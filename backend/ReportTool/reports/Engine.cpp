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
         daily = DataLoader::LoadDailyParallel(*conn, *ctx.threads, logins, range_from, range_to_excl, *ctx.log);
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

   //--- Per-login evaluation --------------------------------------
   std::vector<std::vector<GenericWriter::Cell>> rows;
   rows.reserve(users.size());

   for(const auto& u : users)
   {
      EvalContext ec;
      ec.user        = &u;
      ec.daily       = daily.count(u.login)         ? &daily[u.login]          : nullptr;
      ec.deals       = deals.count(u.login)         ? &deals[u.login]          : nullptr;
      ec.positions   = positions.count(u.login)     ? &positions[u.login]      : nullptr;
      ec.open_orders = open_orders.count(u.login)   ? &open_orders[u.login]    : nullptr;
      ec.history_orders = history_orders.count(u.login) ? &history_orders[u.login] : nullptr;
      ec.account     = accounts.count(u.login)      ? &accounts[u.login]       : nullptr;
      ec.date_params = &date_params;
      ec.filters     = &filters;

      //--- ensure non-null source ptrs even with empty vector when source was fetched
      //--- (so evaluator's `Need(...)` doesn't trip on accounts with no deals).
      static const std::vector<DailyRow>          kEmptyDaily;
      static const std::vector<DealRow>           kEmptyDeals;
      static const std::vector<PositionRow>       kEmptyPos;
      static const std::vector<OpenOrderRow>      kEmptyOO;
      static const std::vector<HistoryOrderRow>   kEmptyOH;
      if(!ec.daily          && need_daily) ec.daily          = &kEmptyDaily;
      if(!ec.deals          && need_deal)  ec.deals          = &kEmptyDeals;
      if(!ec.positions      && need_pos)   ec.positions      = &kEmptyPos;
      if(!ec.open_orders    && need_oo)    ec.open_orders    = &kEmptyOO;
      if(!ec.history_orders && need_oh)    ec.history_orders = &kEmptyOH;
      static const AccountInfo kEmptyAcc;
      if(!ec.account        && need_acc)   ec.account        = &kEmptyAcc;

      std::vector<GenericWriter::Cell> row;
      row.reserve(tpl.columns.size());
      for(const auto& c : tpl.columns)
      {
         if(c.kind == ColumnSpec::Kind::Identifier)
         {
            row.push_back(EvalIdentifier(c, ec));
         }
         else
         {
            try
            {
               const double v = c.expr ? Expression::Evaluate(*c.expr, ec) : 0.0;
               row.push_back(GenericWriter::Cell::Num(v));
            }
            catch(const std::exception& e)
            {
               ctx.log->Warn("login=%llu column='%s': %s", (unsigned long long)u.login, c.key.c_str(), e.what());
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
