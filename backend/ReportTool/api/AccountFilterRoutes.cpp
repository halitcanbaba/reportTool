//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       AccountFilterRoutes.cpp    |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "AccountFilterRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../mt5/DataLoader.h"
#include "../reports/Expression.h"
#include "../reports/FieldCatalog.h"
#include "AppContext.h"

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   json ToJson(const AccountFilter& f)
   {
      json masks = json::array();
      for(const auto& s : f.group_masks) masks.push_back(s);
      return json{
         { "id",             f.id },
         { "name",           f.name },
         { "description",    f.description },
         { "group_masks",    masks },
         { "group_regex",    f.group_regex },
         { "login_min",      f.login_min ? json(f.login_min) : json(nullptr) },
         { "login_max",      f.login_max ? json(f.login_max) : json(nullptr) },
         { "manager_id",     f.manager_id ? json(f.manager_id) : json(nullptr) },
         { "user_predicate", f.user_predicate ? Expression::PredicateToJson(*f.user_predicate) : json(nullptr) },
         { "folder_id",      f.folder_id ? json(f.folder_id) : json(nullptr) },
         { "sort_order",     f.sort_order },
         { "created_at",     f.created_at },
         { "updated_at",     f.updated_at },
      };
   }

   bool FromJson(const json& j, AccountFilter* f, std::string* err)
   {
      try
      {
         f->name        = j.value("name", "");
         f->description = j.value("description", "");
         if(f->name.empty()) { *err = "name is required"; return false; }
         f->group_masks.clear();
         if(j.contains("group_masks") && j["group_masks"].is_array())
            for(const auto& v : j["group_masks"]) if(v.is_string()) f->group_masks.push_back(v.get<std::string>());
         f->group_regex = j.value("group_regex", "");
         f->login_min   = j.contains("login_min") && !j["login_min"].is_null() ? j["login_min"].get<uint64_t>() : 0;
         f->login_max   = j.contains("login_max") && !j["login_max"].is_null() ? j["login_max"].get<uint64_t>() : 0;
         f->manager_id  = j.contains("manager_id") && !j["manager_id"].is_null() ? j["manager_id"].get<int64_t>() : 0;
         f->user_predicate.reset();
         if(j.contains("user_predicate") && !j["user_predicate"].is_null())
         {
            if(!Expression::PredicateFromJson(j["user_predicate"], &f->user_predicate, err))
               return false;
         }
         //--- PATCH semantics: missing key leaves the existing folder_id intact.
         if(j.contains("folder_id"))
         {
            if(j["folder_id"].is_null())                  f->folder_id = 0;
            else if(j["folder_id"].is_number_integer())   f->folder_id = j["folder_id"].get<int64_t>();
         }
      }
      catch(const std::exception& e) { *err = e.what(); return false; }
      return true;
   }

   //--- Validate user_predicate (if any) against User source filterable schema.
   //--- Returns 400-status error message on failure.
   bool ValidateUserPred(const AccountFilter& f, std::string* err)
   {
      if(!f.user_predicate) return true;
      auto errs = FieldCatalog::ValidatePredicateStandalone(*f.user_predicate, FieldCatalog::Source::User);
      if(errs.empty()) return true;
      std::string msg = "user_predicate validation failed: ";
      for(size_t i = 0; i < errs.size() && i < 5; ++i)
         msg += (i ? "; " : "") + errs[i].path + ": " + errs[i].message;
      *err = msg;
      return false;
   }
}

void AccountFilterRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/account-filters", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = AccountFilterRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& r : rows) out.push_back(ToJson(r));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/account-filters/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto f = AccountFilterRepo::Get(*ctx->db, id);
      if(!f) { SendError(res, 404, "account filter not found"); return; }
      res.set_content(ToJson(*f).dump(), "application/json");
   });

   srv.Post("/api/account-filters", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      AccountFilter f; std::string err;
      if(!FromJson(j, &f, &err)) { SendError(res, 400, err); return; }
      if(!ValidateUserPred(f, &err)) { SendError(res, 400, err); return; }
      AccountFilterRepo::Insert(*ctx->db, f);
      res.set_content(ToJson(f).dump(), "application/json");
   });

   srv.Patch(R"(/api/account-filters/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = AccountFilterRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "account filter not found"); return; }
      AccountFilter f = *cur;
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string err;
      if(!FromJson(j, &f, &err)) { SendError(res, 400, err); return; }
      if(!ValidateUserPred(f, &err)) { SendError(res, 400, err); return; }
      f.id = id;
      AccountFilterRepo::Update(*ctx->db, f);
      res.set_content(ToJson(f).dump(), "application/json");
   });

   srv.Delete(R"(/api/account-filters/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      AccountFilterRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   //--- Shared: parse the filter spec from request body + run it against MT5.
   //--- On success fills *out and returns true. On failure writes error to res
   //--- and returns false. Used by both /preview and /preview.csv.
   auto runPreviewFilter = [ctx](
      const json& body, httplib::Response& res,
      std::vector<UserInfo>* out_users, std::vector<std::string>* out_extra_fields) -> bool
   {
      if(!body.is_object())
         { SendError(res, 400, "invalid json"); return false; }
      if(!body.contains("manager_id") || !body["manager_id"].is_number_integer())
         { SendError(res, 400, "manager_id required"); return false; }
      const int64_t mgr_id = body["manager_id"].get<int64_t>();

      auto mgr = ManagerRepo::Get(*ctx->db, mgr_id);
      if(!mgr) { SendError(res, 404, "manager not found"); return false; }

      std::vector<std::string> masks;
      if(body.contains("group_masks") && body["group_masks"].is_array())
         for(const auto& v : body["group_masks"])
            if(v.is_string()) masks.push_back(v.get<std::string>());
      const std::string regex = body.value("group_regex", std::string());
      const uint64_t login_min = body.contains("login_min") && !body["login_min"].is_null()
                                 ? body["login_min"].get<uint64_t>() : 0;
      const uint64_t login_max = body.contains("login_max") && !body["login_max"].is_null()
                                 ? body["login_max"].get<uint64_t>() : 0;

      std::shared_ptr<Predicate> user_pred;
      if(body.contains("user_predicate") && !body["user_predicate"].is_null())
      {
         std::string perr;
         if(!Expression::PredicateFromJson(body["user_predicate"], &user_pred, &perr))
         { SendError(res, 400, std::string("user_predicate: ") + perr); return false; }
         auto errs = FieldCatalog::ValidatePredicateStandalone(*user_pred, FieldCatalog::Source::User);
         if(!errs.empty())
         {
            std::string msg = "user_predicate invalid: " + errs[0].path + ": " + errs[0].message;
            SendError(res, 400, msg);
            return false;
         }
      }

      auto conn = ctx->pool->GetOrConnect(*mgr);
      if(!conn) {
         const std::string emsg = "MT5 connect failed (server=" + mgr->server
                                  + ", login=" + std::to_string(mgr->manager_login)
                                  + "): " + Connection::LastErrorString();
         SendError(res, 502, emsg);
         return false;
      }

      try { *out_users = DataLoader::LoadUsers(*conn, masks, regex, login_min, login_max, *ctx->log); }
      catch(const std::exception& e) { SendError(res, 500, std::string("LoadUsers: ") + e.what()); return false; }

      //--- Apply optional user predicate post-filter (matches Engine semantics).
      if(user_pred)
      {
         out_users->erase(
            std::remove_if(out_users->begin(), out_users->end(),
               [&](const UserInfo& u){
                  try { return !FieldCatalog::EvalUserPredicate(*user_pred, u); }
                  catch(...) { return true; }
               }),
            out_users->end());
         //--- Extra fields to surface (one per distinct user_predicate field).
         *out_extra_fields = FieldCatalog::CollectPredicateFields(*user_pred);
      }
      return true;
   };

   //--- Preview: run the filter against MT5, return matched count + a sliced
   //--- window of rows so the UI can paginate. Distinct-group sample is also
   //--- emitted (cap 25) so the editor can show "group coverage" at a glance.
   srv.Post("/api/account-filters/preview", [ctx, runPreviewFilter](const httplib::Request& req, httplib::Response& res){
      json body = json::parse(req.body, nullptr, false);
      if(body.is_discarded()) { SendError(res, 400, "invalid json"); return; }

      std::vector<UserInfo> users;
      std::vector<std::string> extra_fields;
      if(!runPreviewFilter(body, res, &users, &extra_fields)) return;

      //--- Pagination window. Defaults preserve the previous "first 25" behavior
      //--- for callers that don't pass offset/limit. Limit is clamped to 500 to
      //--- bound JSON payload size; clients that want everything use /preview.csv.
      size_t offset = body.contains("offset") && body["offset"].is_number_integer()
                       ? std::max<int64_t>(0, body["offset"].get<int64_t>()) : 0;
      size_t limit  = body.contains("limit") && body["limit"].is_number_integer()
                       ? std::clamp<int64_t>(body["limit"].get<int64_t>(), 1, 500) : 25;
      if(offset > users.size()) offset = users.size();
      const size_t end = std::min(users.size(), offset + limit);

      json sample_logins = json::array();
      for(size_t i = offset; i < end; ++i)
      {
         json row = {
            { "login", users[i].login },
            { "group", users[i].group },
            { "name",  users[i].name  },
         };
         if(!extra_fields.empty())
         {
            json extra = json::object();
            for(const auto& f : extra_fields)
               extra[f] = FieldCatalog::GetUserFieldString(users[i], f);
            row["extra"] = extra;
         }
         sample_logins.push_back(row);
      }

      //--- Group sample independent of pagination — gives a stable "coverage"
      //--- read across all matched rows, not just the current page.
      json sample_groups = json::array();
      std::unordered_set<std::string> seen_groups;
      const size_t group_limit = 25;
      for(const auto& u : users)
      {
         if(seen_groups.size() >= group_limit) break;
         if(seen_groups.insert(u.group).second) sample_groups.push_back(u.group);
      }

      res.set_content(json{
         { "matched_count", (int64_t)users.size() },
         { "offset",        (int64_t)offset },
         { "limit",         (int64_t)limit  },
         { "sample_logins", sample_logins },
         { "sample_groups", sample_groups },
         { "extra_fields",  extra_fields },
      }.dump(), "application/json");
   });

   //--- CSV export of the full match list. Same body shape as /preview; ignores
   //--- offset/limit and streams every matched row so the user can sanity-check
   //--- a filter in Excel before saving it.
   srv.Post("/api/account-filters/preview.csv", [ctx, runPreviewFilter](const httplib::Request& req, httplib::Response& res){
      json body = json::parse(req.body, nullptr, false);
      if(body.is_discarded()) { SendError(res, 400, "invalid json"); return; }

      std::vector<UserInfo> users;
      std::vector<std::string> extra_fields;
      if(!runPreviewFilter(body, res, &users, &extra_fields)) return;

      auto esc = [](const std::string& s) {
         //--- RFC4180-ish: quote if the field contains comma / quote / CR / LF.
         bool needs = s.find_first_of(",\"\r\n") != std::string::npos;
         if(!needs) return s;
         std::string out = "\"";
         for(char c : s) { if(c == '"') out += '"'; out += c; }
         out += '"';
         return out;
      };

      std::string csv;
      csv.reserve(users.size() * 96);
      //--- Header row
      csv += "login,group,name";
      for(const auto& f : extra_fields) { csv += ','; csv += esc(f); }
      csv += "\r\n";
      //--- Data rows
      for(const auto& u : users)
      {
         csv += std::to_string(u.login);
         csv += ','; csv += esc(u.group);
         csv += ','; csv += esc(u.name);
         for(const auto& f : extra_fields)
         { csv += ','; csv += esc(FieldCatalog::GetUserFieldString(u, f)); }
         csv += "\r\n";
      }

      res.set_header("Content-Disposition", "attachment; filename=\"account_filter_preview.csv\"");
      res.set_content(csv, "text/csv; charset=utf-8");
   });
}
