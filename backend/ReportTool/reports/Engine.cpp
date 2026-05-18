//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Engine.cpp                 |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Engine.h"
#include "Expression.h"
#include "FieldCatalog.h"
#include "GenericWriter.h"
#include "Classifier.h"
#include "../db/Repos.h"
#include "../mt5/DataLoader.h"
#include "../core/RegexCache.h"
#include "../core/TimeUtil.h"
#include <filesystem>
#include <algorithm>

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
   //--- Resolve effective filter from manager + saved AccountFilter + override.
   //--- Priority: override > saved filter > manager defaults.
   struct EffectiveFilter
   {
      std::vector<std::string>   group_masks;
      std::string                group_regex;
      uint64_t                   login_min = 0;
      uint64_t                   login_max = 0;
      std::shared_ptr<Predicate> user_predicate;   // optional IMTUser post-filter
   };

   EffectiveFilter ResolveFilter(const ManagerRow& mgr,
                                 const std::optional<AccountFilter>& af,
                                 const json& override_obj)
   {
      EffectiveFilter ef;
      ef.group_masks = mgr.group_masks;
      ef.group_regex = mgr.group_regex;
      ef.login_min   = mgr.login_min;
      ef.login_max   = mgr.login_max;

      if(af.has_value())
      {
         if(!af->group_masks.empty()) ef.group_masks = af->group_masks;
         if(!af->group_regex.empty()) ef.group_regex = af->group_regex;
         if(af->login_min)            ef.login_min   = af->login_min;
         if(af->login_max)            ef.login_max   = af->login_max;
         if(af->user_predicate)       ef.user_predicate = af->user_predicate;
      }

      if(override_obj.is_object())
      {
         if(override_obj.contains("group_masks") && override_obj["group_masks"].is_array())
         {
            ef.group_masks.clear();
            for(const auto& v : override_obj["group_masks"])
               if(v.is_string()) ef.group_masks.push_back(v.get<std::string>());
         }
         if(override_obj.contains("group_regex") && override_obj["group_regex"].is_string())
            ef.group_regex = override_obj["group_regex"].get<std::string>();
         if(override_obj.contains("login_min") && override_obj["login_min"].is_number_unsigned())
            ef.login_min = override_obj["login_min"].get<uint64_t>();
         if(override_obj.contains("login_max") && override_obj["login_max"].is_number_unsigned())
            ef.login_max = override_obj["login_max"].get<uint64_t>();
         if(override_obj.contains("user_predicate") && !override_obj["user_predicate"].is_null())
         {
            std::shared_ptr<Predicate> p;
            std::string perr;
            if(Expression::PredicateFromJson(override_obj["user_predicate"], &p, &perr))
               ef.user_predicate = p;
         }
      }
      return ef;
   }

   void EnsureDir(const std::string& path)
   {
      std::error_code ec;
      fs::create_directories(path, ec);
   }

   std::vector<uint64_t> LoginsOf(const std::vector<UserInfo>& users)
   {
      std::vector<uint64_t> out; out.reserve(users.size());
      for(const auto& u : users) out.push_back(u.login);
      return out;
   }

   //--- Identifier source field evaluation (Pure text path).
   GenericWriter::Cell EvalIdentifier(const ColumnSpec& c, const EvalContext& ctx)
   {
      try
      {
         const FieldCatalog::Field* f = FieldCatalog::Lookup(c.source);
         if(!f) return GenericWriter::Cell::N();
         if(f->return_type == "int" || f->return_type == "money" || f->return_type == "pct")
         {
            //--- numeric identifier (login, leverage) → number cell
            return GenericWriter::Cell::Num(f->num ? f->num({}, nullptr, ctx) : 0.0);
         }
         if(f->return_type == "date")
            return GenericWriter::Cell::Num(f->num ? f->num({}, nullptr, ctx) : 0.0);
         //--- text
         return GenericWriter::Cell::Txt(FieldCatalog::EvaluateText(c.source, ctx));
      }
      catch(const std::exception&) { return GenericWriter::Cell::N(); }
   }

   //--- Compare two cells for sort. Direction handled by caller.
   bool CellLess(const GenericWriter::Cell& a, const GenericWriter::Cell& b)
   {
      if(a.kind == GenericWriter::Cell::Kind::Number && b.kind == GenericWriter::Cell::Kind::Number)
         return a.number < b.number;
      const std::string& sa = a.text;
      const std::string& sb = b.text;
      return sa < sb;
   }

   //--- Pivot dispatch. Every first-column identifier drives the row dimension
   //--- through the same generic mechanism — no per-field special cases. The
   //--- driver split is purely about *where the key data lives*:
   //---   • User-driver  — bucket users by a key derived from the user record
   //---                    (login, group, name, country, comment, currency, …).
   //---   • Deal-driver  — bucket deal/position/order records by a key on the
   //---                    deal record itself (symbol, ticket).
   //--- "login" is no longer a zero-copy fast path: it just yields per-user
   //--- unique keys, so the user-driver produces one bucket per user — same
   //--- output, owned vectors instead of views. The merge cost is negligible
   //--- versus the upstream MT5 socket fetch.
   struct PivotStrategy
   {
      enum class Driver { User, Deal };
      Driver      driver = Driver::User;
      std::string field_name = "login";
   };

   PivotStrategy ChoosePivot(const ReportTemplate& tpl)
   {
      PivotStrategy s;
      if(tpl.columns.empty()) return s;
      const auto& c0 = tpl.columns[0];
      if(c0.kind != ColumnSpec::Kind::Identifier) return s;
      //--- symbol/ticket are pivot-only identifiers reading their value from
      //--- the deal record being processed — bucket on the deal side.
      if(c0.source == "symbol" || c0.source == "ticket")
         return {PivotStrategy::Driver::Deal, c0.source};
      //--- Any registered user-source identifier (txt accessor present) routes
      //--- through the generic user-driver. Unknown sources fall back to the
      //--- default (bucket by login) — safer than throwing.
      const FieldCatalog::Field* f = FieldCatalog::Lookup(c0.source);
      if(f && f->source == FieldCatalog::Source::User && f->txt)
         return {PivotStrategy::Driver::User, c0.source};
      return s;
   }

   //--- One output row's worth of pivot-bucketed inputs. Views point into
   //--- shared maps when we can (Login fast path); owned vectors hold the
   //--- merged/filtered slice otherwise.
   struct RowContext
   {
      std::string pivot_text;
      double      pivot_num = 0.0;
      const UserInfo*    user    = nullptr;
      const AccountInfo* account = nullptr;
      const std::vector<DailyRow>*          daily_view          = nullptr;
      const std::vector<DealRow>*           deals_view          = nullptr;
      const std::vector<PositionRow>*       positions_view      = nullptr;
      const std::vector<OpenOrderRow>*      open_orders_view    = nullptr;
      const std::vector<HistoryOrderRow>*   history_orders_view = nullptr;
      std::vector<DailyRow>          daily_owned;
      std::vector<DealRow>           deals_owned;
      std::vector<PositionRow>       positions_owned;
      std::vector<OpenOrderRow>      open_orders_owned;
      std::vector<HistoryOrderRow>   history_orders_owned;
   };

   //--- Apply a user-source predicate to narrow the user list. Falls through
   //--- (returns all users) when `pred` is null. Predicate exceptions are
   //--- treated as "exclude this row" to match the per-row try/catch elsewhere.
   std::vector<const UserInfo*> FilterUsers(const std::vector<UserInfo>& users,
                                            const Predicate* pred)
   {
      std::vector<const UserInfo*> out;
      out.reserve(users.size());
      for(const auto& u : users)
      {
         if(pred)
         {
            try { if(!FieldCatalog::EvalUserPredicate(*pred, u)) continue; }
            catch(...) { continue; }
         }
         out.push_back(&u);
      }
      return out;
   }

   bool DealMatches(const Predicate* pred, const DealRow& d)
   {
      if(!pred) return true;
      try { return FieldCatalog::EvalDealPredicate(*pred, d); }
      catch(...) { return false; }
   }

   //--- Append all rows from `src` (a per-login map) onto `dst` for every
   //--- login in `logins_in_bucket`. Used by the user-driver to merge per-user
   //--- collections into a per-bucket owned vector.
   template <class Row>
   void AppendForLogins(std::vector<Row>& dst,
                        const std::unordered_map<uint64_t, std::vector<Row>>& src,
                        const std::vector<uint64_t>& logins_in_bucket)
   {
      for(uint64_t lg : logins_in_bucket)
      {
         auto it = src.find(lg);
         if(it == src.end()) continue;
         dst.insert(dst.end(), it->second.begin(), it->second.end());
      }
   }

   //--- Generic user-driver pivot. Buckets users by a key extracted via
   //--- `key_field.txt(ec)` and merges every per-user collection for that key
   //--- into the bucket's owned vectors. Subsumes the old Login + Group paths.
   //--- The synthetic EvalContext sets both `user` and `daily` so that fields
   //--- which depend on daily data (e.g. `currency`) can compute their key.
   std::vector<RowContext> BuildUserKeyContexts(
      const std::vector<UserInfo>& users,
      const std::unordered_map<uint64_t, std::vector<DailyRow>>&        daily,
      const std::unordered_map<uint64_t, std::vector<DealRow>>&         deals,
      const std::unordered_map<uint64_t, std::vector<PositionRow>>&     positions,
      const std::unordered_map<uint64_t, std::vector<OpenOrderRow>>&    open_orders,
      const std::unordered_map<uint64_t, std::vector<HistoryOrderRow>>& history_orders,
      const std::unordered_map<uint64_t, AccountInfo>&                  accounts,
      const Predicate*                                                  row_pred,
      const FieldCatalog::Field&                                        key_field)
   {
      //--- Stable bucket ordering = first-seen across `users` (post user-filter).
      std::vector<std::string>                               order;
      std::unordered_map<std::string, std::vector<uint64_t>> by_key;
      std::unordered_map<std::string, const UserInfo*>       first_user;

      for(const auto& u : users)
      {
         if(row_pred)
         {
            try { if(!FieldCatalog::EvalUserPredicate(*row_pred, u)) continue; }
            catch(...) { continue; }
         }
         //--- Compute the bucket key by invoking the field's text accessor on
         //--- a per-user synthetic context. Failures (e.g. daily-dependent
         //--- fields with no daily data) bucket the user under "" — same
         //--- collapse semantics as an empty group string.
         std::string key;
         try
         {
            EvalContext ec;
            ec.user = &u;
            auto dit = daily.find(u.login);
            ec.daily = (dit != daily.end()) ? &dit->second : nullptr;
            key = key_field.txt(ec);
         }
         catch(...) { key.clear(); }

         auto& bucket = by_key[key];
         if(bucket.empty())
         {
            order.push_back(key);
            first_user[key] = &u;
         }
         bucket.push_back(u.login);
      }

      std::vector<RowContext> out;
      out.reserve(order.size());
      for(const auto& k : order)
      {
         RowContext rc;
         rc.pivot_text = k;
         rc.pivot_num  = 0.0;
         rc.user       = first_user[k];
         if(rc.user && key_field.num)
         {
            //--- Numeric identifiers (e.g. login) — surface the numeric value
            //--- for any downstream consumer that reads pivot_key_num.
            try
            {
               EvalContext ec;
               ec.user = rc.user;
               auto dit = daily.find(rc.user->login);
               ec.daily = (dit != daily.end()) ? &dit->second : nullptr;
               rc.pivot_num = key_field.num({}, nullptr, ec);
            }
            catch(...) { rc.pivot_num = 0.0; }
         }
         if(rc.user)
         {
            auto ait = accounts.find(rc.user->login);
            rc.account = (ait != accounts.end()) ? &ait->second : nullptr;
         }
         const auto& logins_in_bucket = by_key[k];
         AppendForLogins(rc.daily_owned,          daily,          logins_in_bucket);
         AppendForLogins(rc.deals_owned,          deals,          logins_in_bucket);
         AppendForLogins(rc.positions_owned,      positions,      logins_in_bucket);
         AppendForLogins(rc.open_orders_owned,    open_orders,    logins_in_bucket);
         AppendForLogins(rc.history_orders_owned, history_orders, logins_in_bucket);
         out.push_back(std::move(rc));
      }
      return out;
   }

   //--- Collect distinct symbols (first-seen order) from every loaded row
   //--- that has a `.symbol` field, then emit one RowContext per symbol with
   //--- per-symbol filtered owned vectors. user/account are null — user_*
   //--- columns will fail their Need(...) and surface as blank cells.
   std::vector<RowContext> BuildSymbolContexts(
      const std::unordered_map<uint64_t, std::vector<DealRow>>&         deals,
      const std::unordered_map<uint64_t, std::vector<PositionRow>>&     positions,
      const std::unordered_map<uint64_t, std::vector<OpenOrderRow>>&    open_orders,
      const std::unordered_map<uint64_t, std::vector<HistoryOrderRow>>& history_orders,
      const Predicate*                                                  row_pred)
   {
      //--- row_pred (deal source) narrows which deals contribute to the symbol
      //--- universe AND to each bucket's deals_owned. Positions/orders fall
      //--- through unfiltered (documented "simple" behavior).
      std::vector<std::string>                       order;
      std::unordered_map<std::string, RowContext>    by_sym;
      auto touch = [&](const std::string& s) -> RowContext& {
         auto it = by_sym.find(s);
         if(it == by_sym.end())
         {
            order.push_back(s);
            RowContext rc; rc.pivot_text = s; rc.pivot_num = 0.0;
            it = by_sym.emplace(s, std::move(rc)).first;
         }
         return it->second;
      };
      //--- Deals always go through DealMatches (no-op when row_pred is null).
      for(const auto& kv : deals)
         for(const auto& d : kv.second)
            if(!d.symbol.empty() && DealMatches(row_pred, d))
               touch(d.symbol).deals_owned.push_back(d);
      //--- Positions/orders: with a row_pred, only attach to symbols a matching
      //--- deal already opened (never introduce a symbol with no matching deal);
      //--- without a row_pred, seed freely so position-only symbols still appear.
      const bool gate = row_pred != nullptr;
      for(const auto& kv : positions)
         for(const auto& p : kv.second)
         {
            if(p.symbol.empty()) continue;
            if(gate && !by_sym.count(p.symbol)) continue;
            touch(p.symbol).positions_owned.push_back(p);
         }
      for(const auto& kv : open_orders)
         for(const auto& o : kv.second)
         {
            if(o.symbol.empty()) continue;
            if(gate && !by_sym.count(o.symbol)) continue;
            touch(o.symbol).open_orders_owned.push_back(o);
         }
      for(const auto& kv : history_orders)
         for(const auto& o : kv.second)
         {
            if(o.symbol.empty()) continue;
            if(gate && !by_sym.count(o.symbol)) continue;
            touch(o.symbol).history_orders_owned.push_back(o);
         }

      std::vector<RowContext> out;
      out.reserve(order.size());
      for(const auto& s : order) out.push_back(std::move(by_sym[s]));
      return out;
   }

   //--- One RowContext per deal across every login. user/account taken from
   //--- the deal's owning login so user_*/acc_* fields still resolve.
   std::vector<RowContext> BuildTicketContexts(
      const std::vector<UserInfo>& users,
      const std::unordered_map<uint64_t, std::vector<DealRow>>& deals,
      const std::unordered_map<uint64_t, AccountInfo>& accounts,
      const Predicate*                                 row_pred)
   {
      std::unordered_map<uint64_t, const UserInfo*> user_by_login;
      user_by_login.reserve(users.size());
      for(const auto& u : users) user_by_login[u.login] = &u;

      std::vector<RowContext> out;
      for(const auto& kv : deals)
      {
         const uint64_t lg = kv.first;
         auto uit = user_by_login.find(lg);
         auto ait = accounts.find(lg);
         for(const auto& d : kv.second)
         {
            if(!DealMatches(row_pred, d)) continue;
            RowContext rc;
            rc.pivot_text = std::to_string(d.ticket);
            rc.pivot_num  = (double)d.ticket;
            rc.user    = (uit != user_by_login.end()) ? uit->second : nullptr;
            rc.account = (ait != accounts.end())      ? &ait->second : nullptr;
            rc.deals_owned.push_back(d);
            out.push_back(std::move(rc));
         }
      }
      return out;
   }
}

void Engine::Run(AppContext& ctx, int64_t job_id)
{
   //--- Load job, template, account filter -----------------------
   auto job_opt = JobRepo::Get(*ctx.db, job_id);
   if(!job_opt) throw std::runtime_error("job not found");
   JobRow job = *job_opt;

   auto mgr_opt = ManagerRepo::Get(*ctx.db, job.manager_id);
   if(!mgr_opt) throw std::runtime_error("manager not found");
   ManagerRow mgr = *mgr_opt;

   auto tpl_opt = TemplateRepo::Get(*ctx.db, job.template_id);
   if(!tpl_opt) throw std::runtime_error("template not found");
   ReportTemplate tpl = *tpl_opt;

   //--- Validate template before running.
   auto errs = FieldCatalog::Validate(tpl);
   if(!errs.empty())
   {
      std::string msg = "template validation failed: ";
      for(size_t i = 0; i < errs.size() && i < 5; ++i)
         msg += (i ? "; " : "") + errs[i].path + ": " + errs[i].message;
      throw std::runtime_error(msg);
   }

   std::optional<AccountFilter> af;
   if(job.account_filter_id) af = AccountFilterRepo::Get(*ctx.db, job.account_filter_id);

   //--- Parse params_json -----------------------------------------
   json params = json::parse(job.params_json, nullptr, false);
   if(params.is_discarded()) throw std::runtime_error("bad params_json");

   //--- Resolve date params (string YYYY-MM-DD → Unix midnight) ---
   std::map<std::string, int64_t> date_params;
   const json& dates_obj = params.contains("dates") ? params["dates"] : json::object();
   for(const auto& dp_name : tpl.date_params)
   {
      if(!dates_obj.contains(dp_name) || !dates_obj[dp_name].is_string())
         throw std::runtime_error("missing date param '" + dp_name + "'");
      date_params[dp_name] = TimeUtil::DateStringToTime(dates_obj[dp_name].get<std::string>());
   }

   const uint32_t top_n = params.value("top_n", tpl.default_top_n);

   //--- Effective filter ------------------------------------------
   EffectiveFilter ef = ResolveFilter(mgr, af,
      params.contains("account_filter_override") ? params["account_filter_override"] : json());

   //--- Output dir + connect --------------------------------------
   const std::string job_dir = ctx.cfg.output_dir + "/job_" + std::to_string(job_id);
   EnsureDir(job_dir);

   CompiledFilters filters; std::string ferr;
   if(!CompiledFilters::Compile(mgr.regex_filters, &filters, &ferr))
      throw std::runtime_error("regex: " + ferr);

   auto conn = ctx.pool->GetOrConnect(mgr);
   if(!conn) throw std::runtime_error("connection failed");

   JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Running, 0.10);

   //--- Load users with effective filter --------------------------
   //--- Fresh fetch — no caching at the user level. SDK socket is pooled in
   //--- ConnectionPool for auth efficiency only; each job hits MT5 live.
   ctx.log->Info("Fetching live user list from MT5 (manager=%lld, masks=%zu) ...",
                  (long long)mgr.id, ef.group_masks.size());
   auto users = DataLoader::LoadUsers(*conn, ef.group_masks, ef.group_regex,
                                      ef.login_min, ef.login_max, *ctx.log);

   //--- Apply optional user predicate (account filter post-filter, e.g. comment/agent/zip).
   if(ef.user_predicate)
   {
      const size_t before = users.size();
      users.erase(
         std::remove_if(users.begin(), users.end(),
            [&](const UserInfo& u){
               try { return !FieldCatalog::EvalUserPredicate(*ef.user_predicate, u); }
               catch(const std::exception& e) { ctx.log->Warn("user predicate eval: %s", e.what()); return true; }
            }),
         users.end());
      ctx.log->Info("User predicate filtered %zu -> %zu", before, users.size());
   }
   ctx.log->Info("Fresh user list ready: %zu users", users.size());

   auto logins = LoginsOf(users);
   if(users.empty())
   {
      //--- No accounts matched: emit empty result.
      std::vector<std::vector<GenericWriter::Cell>> empty;
      std::string csv = GenericWriter::WriteCsv(tpl.columns, empty, job_dir, job_id, tpl.name);
      json preview = GenericWriter::ToJson(tpl, empty, 0, 0, 0, 0);
      JobRepo::UpdateOutput(*ctx.db, job_id, job_dir, csv, "", preview.dump());
      JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Completed, 1.0);
      ctx.log->Info("job %lld completed (no accounts matched)", (long long)job_id);
      return;
   }

   JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Running, 0.25);

   //--- AST analysis ----------------------------------------------
   auto analysis = FieldCatalog::Analyze(tpl);

   //--- Daily snapshot targets (D-86400 and D for each snapshot date_param)
   std::vector<int64_t> snapshot_targets;
   for(const auto& dp : analysis.snapshot_date_params)
   {
      auto it = date_params.find(dp);
      if(it == date_params.end()) continue;
      snapshot_targets.push_back(it->second - 86400);  // start variant
      snapshot_targets.push_back(it->second);          // end variant
   }

   //--- Range window across all 2-arity range fields. Pairs are (from, to)
   //--- but we don't know which date param is from vs to per-field — just
   //--- take the union of all referenced dates as a single [min, max+86400) window.
   int64_t range_from = 0, range_to_excl = 0;
   if(!analysis.range_date_params.empty())
   {
      bool first = true;
      for(const auto& dp : analysis.range_date_params)
      {
         auto it = date_params.find(dp);
         if(it == date_params.end()) continue;
         const int64_t t = it->second;
         if(first) { range_from = t; range_to_excl = t + 86400; first = false; }
         else
         {
            if(t < range_from)        range_from = t;
            if(t + 86400 > range_to_excl) range_to_excl = t + 86400;
         }
      }
   }

   const bool need_daily   = analysis.sources.count(FieldCatalog::Source::Daily) > 0;
   const bool need_deal    = analysis.sources.count(FieldCatalog::Source::Deal) > 0;
   const bool need_acc     = analysis.sources.count(FieldCatalog::Source::Account) > 0;
   const bool need_pos     = analysis.sources.count(FieldCatalog::Source::Position) > 0;
   const bool need_oo      = analysis.sources.count(FieldCatalog::Source::OrderOpen) > 0;
   const bool need_oh      = analysis.sources.count(FieldCatalog::Source::OrderHist) > 0;
   const bool has_range    = range_to_excl > range_from;
   const bool has_snapshot = !snapshot_targets.empty();

   //--- Diagnostic: set env REPORTTOOL_DAILY_DIAG=1 to log resolved date params,
   //--- range/snapshot boundaries — read alongside DataLoader's [DAILY-DIAG] lines
   //--- to determine the broker's IMTDaily::Datetime() convention.
   if(need_daily)
   {
      char ebuf[8]; size_t en = 0;
      const bool diag = (getenv_s(&en, ebuf, sizeof(ebuf), "REPORTTOOL_DAILY_DIAG") == 0)
                        && en > 0 && ebuf[0] == '1';
      if(diag)
      {
         ctx.log->Info("[DAILY-DIAG] resolve job=%lld template='%s' has_range=%d has_snapshot=%d",
                       (long long)job_id, tpl.name.c_str(), (int)has_range, (int)has_snapshot);
         for(const auto& kv : date_params)
         {
            ctx.log->Info("[DAILY-DIAG]   date_param '%s' = %lld (%s, %%86400=%lld)",
                          kv.first.c_str(), (long long)kv.second,
                          TimeUtil::FormatDateTime(kv.second).c_str(),
                          (long long)(kv.second % 86400));
         }
         if(has_range)
            ctx.log->Info("[DAILY-DIAG]   range [%lld, %lld) = [%s, %s)",
                          (long long)range_from, (long long)range_to_excl,
                          TimeUtil::FormatDateTime(range_from).c_str(),
                          TimeUtil::FormatDateTime(range_to_excl).c_str());
         for(int64_t t : snapshot_targets)
            ctx.log->Info("[DAILY-DIAG]   snapshot_target %lld (%s)",
                          (long long)t, TimeUtil::FormatDateTime(t).c_str());
      }
   }

   //--- Lazy fetch -------------------------------------------------
   std::unordered_map<uint64_t, std::vector<DailyRow>>       daily;
   std::unordered_map<uint64_t, std::vector<DealRow>>        deals;
   std::unordered_map<uint64_t, AccountInfo>                 accounts;
   std::unordered_map<uint64_t, std::vector<PositionRow>>    positions;
   std::unordered_map<uint64_t, std::vector<OpenOrderRow>>   open_orders;
   std::unordered_map<uint64_t, std::vector<HistoryOrderRow>> history_orders;

   if(need_daily)
   {
      if(has_range)
      {
         //--- When the template mixes range fields AND snapshot fields, the
         //--- daily fetch must cover BOTH the range AND the snapshot windows.
         //--- PickLatestAtOrBefore needs rows up to ~7 days BEFORE each
         //--- snapshot target (weekends / missing days). Without this,
         //--- *_start(date_from) and *_prev_day_*(date_from/date_to) starve
         //--- and fall back to whatever the nearest in-range row happens to be.
         int64_t daily_from = range_from;
         int64_t daily_to_excl = range_to_excl;
         if(has_snapshot)
         {
            const int64_t SNAP_MARGIN = 7 * 86400;
            for(int64_t t : snapshot_targets)
            {
               if(t - SNAP_MARGIN < daily_from)  daily_from    = t - SNAP_MARGIN;
               if(t + 86400      > daily_to_excl) daily_to_excl = t + 86400;
            }
         }
         daily = DataLoader::LoadDailyParallel(*conn, *ctx.threads, logins, daily_from, daily_to_excl, *ctx.log);
      }
      else if(has_snapshot)
         daily = DataLoader::LoadDailyBoundary(*conn, *ctx.threads, logins, snapshot_targets, 7, *ctx.log);
   }
   JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Running, 0.45);

   if(need_deal && has_range)
   {
      deals = DataLoader::LoadDealsParallel(*conn, *ctx.threads, logins, range_from, range_to_excl, *ctx.log);
      //--- Diagnostic: deal action distribution + bucket distribution. Helps the
      //--- user verify that DEAL_BALANCE rows exist and are being classified.
      size_t total = 0, n_balance = 0, n_correction = 0, n_buysell = 0, n_other = 0;
      size_t b_dep = 0, b_wd = 0, b_wo = 0, b_adj = 0, b_none = 0;
      for(const auto& kv : deals)
      {
         for(const auto& d : kv.second)
         {
            ++total;
            if(d.action == IMTDeal::DEAL_BALANCE)         ++n_balance;
            else if(d.action == IMTDeal::DEAL_CORRECTION) ++n_correction;
            else if(d.action == IMTDeal::DEAL_BUY
                 || d.action == IMTDeal::DEAL_SELL)       ++n_buysell;
            else                                          ++n_other;

            const int b = Classifier::Bucket(d, filters);
            if(b ==  1) ++b_dep;
            else if(b == -1) ++b_wd;
            else if(b ==  2) ++b_wo;
            else if(b ==  3) ++b_adj;
            else             ++b_none;
         }
      }
      ctx.log->Info("Deals breakdown: total=%zu | DEAL_BALANCE=%zu CORRECTION=%zu BUY/SELL=%zu other=%zu",
                    total, n_balance, n_correction, n_buysell, n_other);
      ctx.log->Info("Bucket assignment: deposit=%zu withdrawal=%zu writeoff=%zu adjustment=%zu unclassified=%zu",
                    b_dep, b_wd, b_wo, b_adj, b_none);
      if(n_balance > 0 && (b_dep + b_wd + b_wo + b_adj) == 0)
         ctx.log->Warn("All %zu DEAL_BALANCE rows ended up unclassified. "
                       "Either configure regex filters under the manager, or rely on the "
                       "automatic profit-sign fallback (active when those lists are empty).",
                       n_balance);

      //--- Compute trade_lifetime_sec per OUT/OUT_BY deal via position_id
      //--- pairing. Two passes per login: (1) record earliest IN/INOUT time per
      //--- position_id, (2) fill DealRow.trade_lifetime_sec for closing legs.
      //--- Positions whose IN deal is outside the fetched window stay at 0.
      size_t lt_matched = 0, lt_missing = 0;
      for(auto& kv : deals)
      {
         auto& v = kv.second;
         std::unordered_map<uint64_t, int64_t> in_time;
         for(const auto& d : v)
         {
            if(d.position_id == 0) continue;
            if(d.entry == IMTDeal::ENTRY_IN || d.entry == IMTDeal::ENTRY_INOUT)
            {
               auto it = in_time.find(d.position_id);
               if(it == in_time.end() || d.time < it->second)
                  in_time[d.position_id] = d.time;
            }
         }
         for(auto& d : v)
         {
            if(d.entry != IMTDeal::ENTRY_OUT && d.entry != IMTDeal::ENTRY_OUT_BY) continue;
            auto it = in_time.find(d.position_id);
            if(it != in_time.end() && d.time >= it->second)
            { d.trade_lifetime_sec = d.time - it->second; ++lt_matched; }
            else ++lt_missing;
         }
      }
      ctx.log->Info("Trade lifetime pairing: %zu OUT deals matched, %zu without IN in window",
                    lt_matched, lt_missing);
   }
   JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Running, 0.60);

   if(need_acc)
      accounts = DataLoader::LoadAccountsByLogins(*conn, *ctx.threads, logins, *ctx.log);
   if(need_pos)
      positions = DataLoader::LoadPositionsByLogins(*conn, *ctx.threads, logins, *ctx.log);
   if(need_oo)
      open_orders = DataLoader::LoadOpenOrdersByLogins(*conn, *ctx.threads, logins, *ctx.log);
   if(need_oh && has_range)
      history_orders = DataLoader::LoadOrderHistoryParallel(*conn, *ctx.threads, logins, range_from, range_to_excl, *ctx.log);

   JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Running, 0.80);

   //--- Pivot-aware row generation --------------------------------
   //--- Whatever identifier sits in the first column drives the row dimension.
   //--- ChoosePivot routes user-source identifiers (login, group, country,
   //--- comment, currency, status, …) through the generic user-key builder, and
   //--- routes symbol/ticket through their deal-driven builders. The optional
   //--- row_predicate (designer's "Filter rows" panel) narrows the upstream
   //--- collection (users for user-driver, deals for deal-driver).
   const PivotStrategy strategy = ChoosePivot(tpl);
   const Predicate* row_pred = (!tpl.columns.empty() && tpl.columns[0].row_predicate)
                                  ? tpl.columns[0].row_predicate.get()
                                  : nullptr;

   std::vector<RowContext> contexts;
   switch(strategy.driver)
   {
      case PivotStrategy::Driver::User:
      {
         const FieldCatalog::Field* f = FieldCatalog::Lookup(strategy.field_name);
         //--- ChoosePivot guarantees a user-source field with a txt accessor,
         //--- but guard defensively against catalog/Template drift.
         if(!f || !f->txt)
         {
            const FieldCatalog::Field* lf = FieldCatalog::Lookup("login");
            if(!lf) throw std::runtime_error("FieldCatalog: 'login' missing");
            f = lf;
         }
         contexts = BuildUserKeyContexts(users, daily, deals, positions,
                                         open_orders, history_orders, accounts,
                                         row_pred, *f);
         break;
      }
      case PivotStrategy::Driver::Deal:
         if(strategy.field_name == "symbol")
            contexts = BuildSymbolContexts(deals, positions, open_orders, history_orders, row_pred);
         else  // "ticket"
            contexts = BuildTicketContexts(users, deals, accounts, row_pred);
         break;
   }

   std::vector<std::vector<GenericWriter::Cell>> rows;
   rows.reserve(contexts.size());

   static const std::vector<DailyRow>          kEmptyDaily;
   static const std::vector<DealRow>           kEmptyDeals;
   static const std::vector<PositionRow>       kEmptyPos;
   static const std::vector<OpenOrderRow>      kEmptyOO;
   static const std::vector<HistoryOrderRow>   kEmptyOH;
   static const AccountInfo                    kEmptyAcc;

   for(const auto& rc : contexts)
   {
      EvalContext ec;
      ec.user           = rc.user;
      ec.account        = rc.account;
      ec.daily          = rc.daily_view          ? rc.daily_view          : &rc.daily_owned;
      ec.deals          = rc.deals_view          ? rc.deals_view          : &rc.deals_owned;
      ec.positions      = rc.positions_view      ? rc.positions_view      : &rc.positions_owned;
      ec.open_orders    = rc.open_orders_view    ? rc.open_orders_view    : &rc.open_orders_owned;
      ec.history_orders = rc.history_orders_view ? rc.history_orders_view : &rc.history_orders_owned;
      ec.date_params    = &date_params;
      ec.filters        = &filters;
      ec.pivot_key_text = rc.pivot_text;
      ec.pivot_key_num  = rc.pivot_num;

      //--- preserve the "non-null vector if source was fetched" invariant —
      //--- aggregators do Need(...) which throws on null but tolerates empty.
      if(!ec.daily          && need_daily) ec.daily          = &kEmptyDaily;
      if(!ec.deals          && need_deal)  ec.deals          = &kEmptyDeals;
      if(!ec.positions      && need_pos)   ec.positions      = &kEmptyPos;
      if(!ec.open_orders    && need_oo)    ec.open_orders    = &kEmptyOO;
      if(!ec.history_orders && need_oh)    ec.history_orders = &kEmptyOH;
      if(!ec.account        && need_acc)   ec.account        = &kEmptyAcc;

      std::vector<GenericWriter::Cell> row;
      row.reserve(tpl.columns.size());

      std::unordered_map<std::string, double> col_values;
      col_values.reserve(tpl.columns.size());
      ec.column_values = &col_values;

      for(const auto& c : tpl.columns)
      {
         if(c.kind == ColumnSpec::Kind::Identifier)
         {
            GenericWriter::Cell cell = EvalIdentifier(c, ec);
            if(cell.kind == GenericWriter::Cell::Kind::Number)
               col_values[c.key] = cell.number;
            row.push_back(std::move(cell));
         }
         else
         {
            try
            {
               const double v = c.expr ? Expression::Evaluate(*c.expr, ec) : 0.0;
               col_values[c.key] = v;
               row.push_back(GenericWriter::Cell::Num(v));
            }
            catch(const std::exception& e)
            {
               ctx.log->Warn("pivot='%s' column='%s': %s",
                             rc.pivot_text.c_str(), c.key.c_str(), e.what());
               row.push_back(GenericWriter::Cell::N());
            }
         }
      }
      rows.push_back(std::move(row));
   }

   //--- Sort + top_n ----------------------------------------------
   if(!tpl.sort.column_key.empty())
   {
      int sort_idx = -1;
      for(size_t i = 0; i < tpl.columns.size(); ++i)
         if(tpl.columns[i].key == tpl.sort.column_key) { sort_idx = (int)i; break; }
      if(sort_idx >= 0)
      {
         const bool desc = tpl.sort.descending;
         std::sort(rows.begin(), rows.end(),
            [sort_idx, desc](const auto& a, const auto& b) {
               const auto& ca = (size_t)sort_idx < a.size() ? a[sort_idx] : GenericWriter::Cell::N();
               const auto& cb = (size_t)sort_idx < b.size() ? b[sort_idx] : GenericWriter::Cell::N();
               return desc ? CellLess(cb, ca) : CellLess(ca, cb);
            });
      }
   }
   if(top_n > 0 && rows.size() > top_n) rows.resize(top_n);

   //--- Write outputs ---------------------------------------------
   const std::string csv = GenericWriter::WriteCsv(tpl.columns, rows, job_dir, job_id, tpl.name);

   int64_t out_from = 0, out_to = 0;
   if(!date_params.empty())
   {
      bool first = true;
      for(const auto& kv : date_params)
      {
         if(first) { out_from = kv.second; out_to = kv.second; first = false; }
         else      { if(kv.second < out_from) out_from = kv.second;
                     if(kv.second > out_to)   out_to   = kv.second; }
      }
   }

   const json preview = GenericWriter::ToJson(tpl, rows, (uint32_t)users.size(),
                                              out_from, out_to, 200 /* preview cap */);

   JobRepo::UpdateOutput(*ctx.db, job_id, job_dir, csv, "", preview.dump());
   JobRepo::UpdateStatus(*ctx.db, job_id, JobStatus::Completed, 1.0);
   ctx.log->Info("job %lld completed (rows=%zu, total_logins=%zu)",
                  (long long)job_id, rows.size(), users.size());
}
