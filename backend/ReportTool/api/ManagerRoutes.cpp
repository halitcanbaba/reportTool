//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       ManagerRoutes.cpp          |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "ManagerRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../core/RegexCache.h"
#include "../mt5/ConnectionPool.h"
#include "../mt5/DataLoader.h"
#include <unordered_set>

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg, const std::string& code = "")
   {
      json j = { { "error", msg }, { "code", code } };
      res.status = status;
      res.set_content(j.dump(), "application/json");
   }

   json RegexFiltersToJson(const RegexFilters& f)
   {
      return json{
         { "deposit",    f.deposit    },
         { "withdrawal", f.withdrawal },
         { "writeoff",   f.writeoff   },
         { "adjustment", f.adjustment },
      };
   }

   RegexFilters JsonToRegexFilters(const json& j)
   {
      RegexFilters f;
      auto pull = [&](const char* k, std::vector<std::string>* dst) {
         if(j.contains(k) && j[k].is_array())
            for(const auto& s : j[k]) if(s.is_string()) dst->push_back(s.get<std::string>());
      };
      pull("deposit",    &f.deposit);
      pull("withdrawal", &f.withdrawal);
      pull("writeoff",   &f.writeoff);
      pull("adjustment", &f.adjustment);
      return f;
   }

   json ManagerToJson(const ManagerRow& m)
   {
      json group_masks = json::array();
      for(const auto& s : m.group_masks) group_masks.push_back(s);
      return json{
         { "id",             m.id },
         { "name",           m.name },
         { "brand",          m.brand },
         { "region",         m.region },
         { "server",         m.server },
         { "manager_login",  m.manager_login },
         { "group_masks",    group_masks },
         { "group_regex",    m.group_regex },
         { "login_min",      m.login_min ? json(m.login_min) : json(nullptr) },
         { "login_max",      m.login_max ? json(m.login_max) : json(nullptr) },
         { "active",         m.active },
         { "created_at",     m.created_at },
         { "updated_at",     m.updated_at },
         { "regex_filters",  RegexFiltersToJson(m.regex_filters) },
      };
   }

   bool JsonToManager(const json& j, ManagerRow* m, std::string* err)
   {
      try
      {
         if(j.contains("name"))           m->name = j["name"].get<std::string>();
         if(j.contains("brand"))          m->brand = j["brand"].get<std::string>();
         if(j.contains("region"))         m->region = j["region"].get<std::string>();
         if(j.contains("server"))         m->server = j["server"].get<std::string>();
         if(j.contains("manager_login"))  m->manager_login = j["manager_login"].get<uint64_t>();
         if(j.contains("password") && j["password"].is_string())
            m->password = j["password"].get<std::string>();
         if(j.contains("group_masks") && j["group_masks"].is_array())
         {
            m->group_masks.clear();
            for(const auto& v : j["group_masks"]) if(v.is_string()) m->group_masks.push_back(v.get<std::string>());
         }
         if(j.contains("group_regex"))    m->group_regex = j.value("group_regex", "");
         if(j.contains("login_min") && !j["login_min"].is_null()) m->login_min = j["login_min"].get<uint64_t>(); else m->login_min = 0;
         if(j.contains("login_max") && !j["login_max"].is_null()) m->login_max = j["login_max"].get<uint64_t>(); else m->login_max = 0;
         if(j.contains("active"))         m->active = j["active"].get<bool>();
         if(j.contains("regex_filters"))  m->regex_filters = JsonToRegexFilters(j["regex_filters"]);
      }
      catch(const std::exception& e) { *err = e.what(); return false; }

      //--- validate regex syntax up front
      CompiledFilters cf; std::string verr;
      if(!CompiledFilters::Compile(m->regex_filters, &cf, &verr))
      {
         *err = verr; return false;
      }
      if(!m->group_regex.empty())
      {
         try { std::regex r(m->group_regex, std::regex::ECMAScript | std::regex::icase); }
         catch(const std::regex_error& e) { *err = std::string("group_regex: ") + e.what(); return false; }
      }
      return true;
   }
}

void ManagerRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/managers", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = ManagerRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& r : rows) out.push_back(ManagerToJson(r));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/managers/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto m = ManagerRepo::Get(*ctx->db, id);
      if(!m) { SendError(res, 404, "manager not found"); return; }
      res.set_content(ManagerToJson(*m).dump(), "application/json");
   });

   srv.Post("/api/managers", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      ManagerRow m; std::string err;
      if(!JsonToManager(j, &m, &err)) { SendError(res, 400, err, "validation"); return; }
      if(m.password.empty()) { SendError(res, 400, "password required"); return; }
      try { ManagerRepo::Insert(*ctx->db, m); }
      catch(const std::exception& e) { SendError(res, 500, e.what()); return; }
      res.set_content(ManagerToJson(m).dump(), "application/json");
   });

   srv.Patch(R"(/api/managers/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = ManagerRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "manager not found"); return; }
      ManagerRow m = *cur;

      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      const bool update_password = j.contains("password") && j["password"].is_string()
                                && !j["password"].get<std::string>().empty();
      std::string err;
      if(!JsonToManager(j, &m, &err)) { SendError(res, 400, err, "validation"); return; }
      m.id = id;
      ManagerRepo::Update(*ctx->db, m, update_password);
      ctx->pool->Drop(id);  // force reconnect on next use
      res.set_content(ManagerToJson(m).dump(), "application/json");
   });

   srv.Delete(R"(/api/managers/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      ManagerRepo::Delete(*ctx->db, id);
      ctx->pool->Drop(id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   srv.Get(R"(/api/managers/(\d+)/groups)", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto m = ManagerRepo::Get(*ctx->db, id);
      if(!m) { SendError(res, 404, "manager not found"); return; }
      auto conn = ctx->pool->GetOrConnect(*m);
      if(!conn) {
         const std::string msg = "MT5 connect failed (server=" + m->server
                                  + ", login=" + std::to_string(m->manager_login)
                                  + "): " + Connection::LastErrorString();
         SendError(res, 502, msg);
         return;
      }

      //--- Enumerate distinct groups visible to this manager (wildcard mask "*").
      std::vector<UserInfo> users;
      try { users = DataLoader::LoadUsers(*conn, {"*"}, "", 0, 0, *ctx->log); }
      catch(const std::exception& e) { SendError(res, 500, std::string("LoadUsers: ") + e.what()); return; }

      std::vector<std::string> groups;
      std::unordered_set<std::string> seen;
      const size_t cap = 500;
      for(const auto& u : users)
      {
         if(seen.size() >= cap) break;
         if(seen.insert(u.group).second) groups.push_back(u.group);
      }
      std::sort(groups.begin(), groups.end());
      res.set_content(json{ {"groups", groups} }.dump(), "application/json");
   });

   srv.Post(R"(/api/managers/(\d+)/test)", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto m = ManagerRepo::Get(*ctx->db, id);
      if(!m) { SendError(res, 404, "manager not found"); return; }
      auto conn = ctx->pool->GetOrConnect(*m);
      json j;
      if(!conn) { j = { {"connected", false}, {"error", Connection::LastErrorString()},
                        {"server", m->server}, {"login", m->manager_login} };
                  res.set_content(j.dump(), "application/json"); return; }

      //--- quick probe: pull a small users sample
      auto users = DataLoader::LoadUsers(*conn, m->group_masks, m->group_regex,
                                         m->login_min, m->login_max, *ctx->log);
      //--- distinct groups for diagnostics (max 25)
      std::vector<std::string> groups;
      {
         std::unordered_set<std::string> seen;
         for(const auto& u : users){
            if(seen.insert(u.group).second){
               groups.push_back(u.group);
               if(groups.size() >= 25) break;
            }
         }
      }
      j = { {"connected", true},
            {"users_sample", (int64_t)std::min<size_t>(users.size(), 1000)},
            {"groups_sample", groups} };
      res.set_content(j.dump(), "application/json");
   });
}
