//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       DealFilterRoutes.cpp       |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "DealFilterRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../mt5/DataLoader.h"
#include "../reports/Expression.h"
#include "../reports/FieldCatalog.h"
#include "../core/TimeUtil.h"
#include "AppContext.h"
#include <algorithm>
#include <unordered_set>

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   //--- "Cash-flow" deal actions = everything that affects balance/credit but
   //--- isn't a trade. Trade actions (DEAL_BUY/DEAL_SELL and their cancels)
   //--- are intentionally excluded so the preview surfaces what the user is
   //--- actually trying to classify.
   const std::unordered_set<uint32_t>& CashFlowActions()
   {
      static const std::unordered_set<uint32_t> s = {
         IMTDeal::DEAL_BALANCE,
         IMTDeal::DEAL_CREDIT,
         IMTDeal::DEAL_CHARGE,
         IMTDeal::DEAL_CORRECTION,
         IMTDeal::DEAL_BONUS,
         IMTDeal::DEAL_COMMISSION,
         IMTDeal::DEAL_COMMISSION_DAILY,
         IMTDeal::DEAL_COMMISSION_MONTHLY,
         IMTDeal::DEAL_AGENT,
         IMTDeal::DEAL_AGENT_DAILY,
         IMTDeal::DEAL_AGENT_MONTHLY,
         IMTDeal::DEAL_INTERESTRATE,
         IMTDeal::DEAL_DIVIDEND,
         IMTDeal::DEAL_DIVIDEND_FRANKED,
         IMTDeal::DEAL_TAX,
         IMTDeal::DEAL_SO_COMPENSATION,
         IMTDeal::DEAL_SO_COMPENSATION_CREDIT,
      };
      return s;
   }

   const char* ActionLabel(uint32_t a)
   {
      switch(a)
      {
         case IMTDeal::DEAL_BALANCE:                 return "DEAL_BALANCE";
         case IMTDeal::DEAL_CREDIT:                  return "DEAL_CREDIT";
         case IMTDeal::DEAL_CHARGE:                  return "DEAL_CHARGE";
         case IMTDeal::DEAL_CORRECTION:              return "DEAL_CORRECTION";
         case IMTDeal::DEAL_BONUS:                   return "DEAL_BONUS";
         case IMTDeal::DEAL_COMMISSION:              return "DEAL_COMMISSION";
         case IMTDeal::DEAL_COMMISSION_DAILY:        return "DEAL_COMMISSION_DAILY";
         case IMTDeal::DEAL_COMMISSION_MONTHLY:      return "DEAL_COMMISSION_MONTHLY";
         case IMTDeal::DEAL_AGENT:                   return "DEAL_AGENT";
         case IMTDeal::DEAL_AGENT_DAILY:             return "DEAL_AGENT_DAILY";
         case IMTDeal::DEAL_AGENT_MONTHLY:           return "DEAL_AGENT_MONTHLY";
         case IMTDeal::DEAL_INTERESTRATE:            return "DEAL_INTERESTRATE";
         case IMTDeal::DEAL_DIVIDEND:                return "DEAL_DIVIDEND";
         case IMTDeal::DEAL_DIVIDEND_FRANKED:        return "DEAL_DIVIDEND_FRANKED";
         case IMTDeal::DEAL_TAX:                     return "DEAL_TAX";
         case IMTDeal::DEAL_SO_COMPENSATION:         return "DEAL_SO_COMPENSATION";
         case IMTDeal::DEAL_SO_COMPENSATION_CREDIT:  return "DEAL_SO_COMPENSATION_CREDIT";
         case IMTDeal::DEAL_BUY:                     return "DEAL_BUY";
         case IMTDeal::DEAL_SELL:                    return "DEAL_SELL";
         default:                                    return "UNKNOWN";
      }
   }

   json ToJson(const DealFilter& f)
   {
      return json{
         { "id",          f.id },
         { "name",        f.name },
         { "description", f.description },
         { "predicate",   f.predicate ? Expression::PredicateToJson(*f.predicate) : json(nullptr) },
         { "sort_order",  f.sort_order },
         { "created_at",  f.created_at },
         { "updated_at",  f.updated_at },
      };
   }

   bool FromJson(const json& j, DealFilter* f, std::string* err)
   {
      try
      {
         f->name        = j.value("name", "");
         f->description = j.value("description", "");
         if(f->name.empty()) { *err = "name is required"; return false; }
         f->predicate.reset();
         if(j.contains("predicate") && !j["predicate"].is_null())
         {
            if(!Expression::PredicateFromJson(j["predicate"], &f->predicate, err))
               return false;
         }
         if(!f->predicate) { *err = "predicate is required"; return false; }
         auto errs = FieldCatalog::ValidatePredicateStandalone(*f->predicate, FieldCatalog::Source::Deal);
         if(!errs.empty())
         {
            std::string msg = "predicate invalid: ";
            for(size_t i = 0; i < errs.size() && i < 5; ++i)
               msg += (i ? "; " : "") + errs[i].path + ": " + errs[i].message;
            *err = msg;
            return false;
         }
      }
      catch(const std::exception& e) { *err = e.what(); return false; }
      return true;
   }

   //--- Resolve preview filter spec: pull user list + the candidate deal
   //--- predicate from the body. On error writes 4xx to res and returns false.
   struct PreviewSpec {
      ManagerRow                  mgr;
      std::vector<uint64_t>       logins;
      std::shared_ptr<Predicate>  deal_pred;     // candidate predicate to evaluate
      int64_t                     date_from = 0; // unix s
      int64_t                     date_to   = 0; // unix s (end of day)
      size_t                      offset    = 0;
      size_t                      limit     = 50;
   };

   bool ResolvePreviewSpec(AppContext& ctx, const json& body,
                           httplib::Response& res, PreviewSpec* spec)
   {
      if(!body.is_object()) { SendError(res, 400, "invalid json"); return false; }

      //--- date_from/date_to are mandatory.
      if(!body.contains("date_from") || !body["date_from"].is_string()
       || !body.contains("date_to")   || !body["date_to"].is_string())
      { SendError(res, 400, "date_from / date_to required (YYYY-MM-DD)"); return false; }
      spec->date_from = TimeUtil::DateStringToTime(body["date_from"].get<std::string>());
      spec->date_to   = TimeUtil::DateStringToTime(body["date_to"].get<std::string>()) + 86399; // inclusive end-of-day

      //--- Optionally use a saved account filter as the user-filter spec.
      std::vector<std::string>   masks;
      std::string                regex;
      uint64_t                   login_min = 0, login_max = 0;
      std::shared_ptr<Predicate> user_pred;
      int64_t                    mgr_id = 0;

      if(body.contains("account_filter_id") && body["account_filter_id"].is_number_integer())
      {
         const int64_t afid = body["account_filter_id"].get<int64_t>();
         auto af = AccountFilterRepo::Get(*ctx.db, afid);
         if(!af) { SendError(res, 404, "account filter not found"); return false; }
         masks      = af->group_masks;
         regex      = af->group_regex;
         login_min  = af->login_min;
         login_max  = af->login_max;
         user_pred  = af->user_predicate;
         mgr_id     = af->manager_id;
      }

      //--- Body's inline fields override (or supply) when not coming from a
      //--- saved filter.
      if(body.contains("group_masks") && body["group_masks"].is_array())
      {
         masks.clear();
         for(const auto& v : body["group_masks"])
            if(v.is_string()) masks.push_back(v.get<std::string>());
      }
      if(body.contains("group_regex") && body["group_regex"].is_string())
         regex = body["group_regex"].get<std::string>();
      if(body.contains("login_min") && !body["login_min"].is_null())
         login_min = body["login_min"].get<uint64_t>();
      if(body.contains("login_max") && !body["login_max"].is_null())
         login_max = body["login_max"].get<uint64_t>();
      if(body.contains("manager_id") && body["manager_id"].is_number_integer())
         mgr_id = body["manager_id"].get<int64_t>();

      if(mgr_id == 0) { SendError(res, 400, "manager_id required"); return false; }
      auto mgr = ManagerRepo::Get(*ctx.db, mgr_id);
      if(!mgr) { SendError(res, 404, "manager not found"); return false; }
      spec->mgr = *mgr;

      //--- Candidate deal predicate (the one being designed). Optional —
      //--- when null every row simply gets matched=false.
      if(body.contains("predicate") && !body["predicate"].is_null())
      {
         std::string perr;
         if(!Expression::PredicateFromJson(body["predicate"], &spec->deal_pred, &perr))
         { SendError(res, 400, std::string("predicate: ") + perr); return false; }
         auto errs = FieldCatalog::ValidatePredicateStandalone(*spec->deal_pred, FieldCatalog::Source::Deal);
         if(!errs.empty())
         {
            std::string msg = "predicate invalid: " + errs[0].path + ": " + errs[0].message;
            SendError(res, 400, msg);
            return false;
         }
      }

      //--- Pagination window.
      spec->offset = body.contains("offset") && body["offset"].is_number_integer()
                     ? std::max<int64_t>(0, body["offset"].get<int64_t>()) : 0;
      spec->limit  = body.contains("limit") && body["limit"].is_number_integer()
                     ? std::clamp<int64_t>(body["limit"].get<int64_t>(), 1, 500) : 50;

      //--- Load users -> logins.
      auto conn = ctx.pool->GetOrConnect(*mgr);
      if(!conn) {
         SendError(res, 502, std::string("MT5 connect failed: ") + Connection::LastErrorString());
         return false;
      }
      std::vector<UserInfo> users;
      try { users = DataLoader::LoadUsers(*conn, masks, regex, login_min, login_max, *ctx.log); }
      catch(const std::exception& e) { SendError(res, 500, std::string("LoadUsers: ") + e.what()); return false; }
      if(user_pred)
      {
         users.erase(
            std::remove_if(users.begin(), users.end(),
               [&](const UserInfo& u){
                  try { return !FieldCatalog::EvalUserPredicate(*user_pred, u); }
                  catch(...) { return true; }
               }),
            users.end());
      }
      spec->logins.reserve(users.size());
      for(const auto& u : users) spec->logins.push_back(u.login);
      return true;
   }

   //--- Load all cash-flow deals for the spec's login set in the date range,
   //--- sort by time ascending, evaluate the candidate predicate per row.
   //--- Output: { total_count, matched_count, all_rows } where all_rows is
   //--- ordered and ready to slice/stream.
   struct DealRowOut {
      int64_t      time;
      uint64_t     login;
      uint32_t     action;
      double       profit;
      std::string  comment;
      bool         matched;
   };

   bool LoadAndEvaluateDeals(AppContext& ctx, const PreviewSpec& spec,
                             httplib::Response& res,
                             std::vector<DealRowOut>* out_rows,
                             int64_t* out_matched_count)
   {
      *out_matched_count = 0;
      out_rows->clear();
      if(spec.logins.empty()) return true;     // empty result is fine

      auto conn = ctx.pool->GetOrConnect(spec.mgr);
      if(!conn) {
         SendError(res, 502, std::string("MT5 connect failed: ") + Connection::LastErrorString());
         return false;
      }
      std::unordered_map<uint64_t, std::vector<DealRow>> by_login;
      try {
         by_login = DataLoader::LoadDealsParallel(
            *conn, *ctx.threads, spec.logins, spec.date_from, spec.date_to, *ctx.log);
      }
      catch(const std::exception& e) {
         SendError(res, 500, std::string("LoadDeals: ") + e.what()); return false;
      }

      const auto& cash_set = CashFlowActions();
      for(auto& kv : by_login)
      {
         for(auto& d : kv.second)
         {
            if(cash_set.find(d.action) == cash_set.end()) continue;
            bool matched = false;
            if(spec.deal_pred)
            {
               try { matched = FieldCatalog::EvalDealPredicate(*spec.deal_pred, d); }
               catch(...) { matched = false; }
            }
            out_rows->push_back(DealRowOut{
               (int64_t)d.time, d.login, d.action, d.profit, d.comment, matched
            });
            if(matched) ++(*out_matched_count);
         }
      }
      //--- Newest first — typical "what just happened" UX.
      std::sort(out_rows->begin(), out_rows->end(),
                [](const DealRowOut& a, const DealRowOut& b){ return a.time > b.time; });
      return true;
   }
}

void DealFilterRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/deal-filters", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = DealFilterRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& r : rows) out.push_back(ToJson(r));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/deal-filters/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto f = DealFilterRepo::Get(*ctx->db, id);
      if(!f) { SendError(res, 404, "deal filter not found"); return; }
      res.set_content(ToJson(*f).dump(), "application/json");
   });

   srv.Post("/api/deal-filters", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      DealFilter f; std::string err;
      if(!FromJson(j, &f, &err)) { SendError(res, 400, err); return; }
      DealFilterRepo::Insert(*ctx->db, f);
      res.set_content(ToJson(f).dump(), "application/json");
   });

   srv.Patch(R"(/api/deal-filters/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = DealFilterRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "deal filter not found"); return; }
      DealFilter f = *cur;
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string err;
      if(!FromJson(j, &f, &err)) { SendError(res, 400, err); return; }
      f.id = id;
      DealFilterRepo::Update(*ctx->db, f);
      res.set_content(ToJson(f).dump(), "application/json");
   });

   srv.Delete(R"(/api/deal-filters/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      DealFilterRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   //--- Preview: cash-flow deals in (account filter, date range), with
   //--- per-row "matched by candidate predicate" flag. Pagination via
   //--- offset/limit; group sample / extra-fields are not returned because
   //--- the deal-source has no equivalent "distinct group" aggregation.
   srv.Post("/api/deal-filters/preview", [ctx](const httplib::Request& req, httplib::Response& res){
      json body = json::parse(req.body, nullptr, false);
      if(body.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      PreviewSpec spec;
      if(!ResolvePreviewSpec(*ctx, body, res, &spec)) return;

      std::vector<DealRowOut> all;
      int64_t matched = 0;
      if(!LoadAndEvaluateDeals(*ctx, spec, res, &all, &matched)) return;

      const size_t total = all.size();
      size_t offset = std::min<size_t>(spec.offset, total);
      const size_t end = std::min<size_t>(total, offset + spec.limit);

      json rows = json::array();
      for(size_t i = offset; i < end; ++i)
      {
         const auto& r = all[i];
         rows.push_back(json{
            { "time",         r.time },
            { "login",        r.login },
            { "action",       r.action },
            { "action_label", ActionLabel(r.action) },
            { "profit",       r.profit },
            { "comment",      r.comment },
            { "matched",      r.matched },
         });
      }

      res.set_content(json{
         { "total_count",   (int64_t)total },
         { "matched_count", matched },
         { "offset",        (int64_t)offset },
         { "limit",         (int64_t)spec.limit },
         { "rows",          rows },
      }.dump(), "application/json");
   });

   //--- CSV export of every cash-flow deal in the window with the matched
   //--- flag. Useful for sanity-checking a candidate predicate in Excel
   //--- before saving it. RFC4180-ish quoting.
   srv.Post("/api/deal-filters/preview.csv", [ctx](const httplib::Request& req, httplib::Response& res){
      json body = json::parse(req.body, nullptr, false);
      if(body.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      PreviewSpec spec;
      if(!ResolvePreviewSpec(*ctx, body, res, &spec)) return;
      std::vector<DealRowOut> all;
      int64_t matched = 0;
      if(!LoadAndEvaluateDeals(*ctx, spec, res, &all, &matched)) return;

      auto esc = [](const std::string& s) {
         bool needs = s.find_first_of(",\"\r\n") != std::string::npos;
         if(!needs) return s;
         std::string out = "\"";
         for(char c : s) { if(c == '"') out += '"'; out += c; }
         out += '"';
         return out;
      };

      std::string csv;
      csv.reserve(all.size() * 96);
      csv += "time_utc,login,action,profit,matched,comment\r\n";
      for(const auto& r : all)
      {
         //--- "YYYY-MM-DD HH:MM:SS" (UTC) for the time column — Excel/Sheets
         //--- recognises this as a datetime.
         char buf[24];
         time_t tt = (time_t)r.time;
         std::tm tm{};
         #ifdef _WIN32
            gmtime_s(&tm, &tt);
         #else
            gmtime_r(&tt, &tm);
         #endif
         std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec);
         csv += buf; csv += ',';
         csv += std::to_string(r.login); csv += ',';
         csv += ActionLabel(r.action); csv += ',';
         {
            char pb[32]; std::snprintf(pb, sizeof(pb), "%.2f", r.profit); csv += pb;
         }
         csv += ',';
         csv += (r.matched ? "true" : "false"); csv += ',';
         csv += esc(r.comment);
         csv += "\r\n";
      }

      res.set_header("Content-Disposition", "attachment; filename=\"deal_filter_preview.csv\"");
      res.set_content(csv, "text/csv; charset=utf-8");
   });
}
