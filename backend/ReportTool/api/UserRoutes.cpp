//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       UserRoutes.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "UserRoutes.h"
#include "CurrentUser.h"
#include "../third_party/httplib.h"
#include "../core/Crypto.h"
#include "../core/SessionManager.h"
#include "../db/Repos.h"

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   json UserToJson(const User& u)
   {
      return json{
         { "id",            u.id },
         { "username",      u.username },
         { "role",          u.role },
         { "active",        u.active },
         { "created_at",    u.created_at },
         { "updated_at",    u.updated_at },
         { "last_active_at", u.last_active_at },
      };
   }

   bool ValidRole(const std::string& r) { return r == "admin" || r == "viewer"; }

   bool ValidatePassword(const std::string& pw, std::string* err)
   {
      if(pw.size() < 6)   { *err = "password must be at least 6 characters"; return false; }
      if(pw.size() > 256) { *err = "password too long";                       return false; }
      return true;
   }

   bool ValidateUsername(const std::string& u, std::string* err)
   {
      if(u.empty())      { *err = "username required"; return false; }
      if(u.size() > 64)  { *err = "username too long"; return false; }
      for(char c : u)
         if(!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-'))
         { *err = "username may only contain letters, digits, _ . -"; return false; }
      return true;
   }
}

void UserRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   //--- GET /api/users
   srv.Get("/api/users", [ctx](const httplib::Request&, httplib::Response& res){
      const auto rows = UserRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& u : rows) out.push_back(UserToJson(u));
      res.set_content(out.dump(), "application/json");
   });

   //--- GET /api/users/:id
   srv.Get(R"(/api/users/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto u = UserRepo::Get(*ctx->db, id);
      if(!u) { SendError(res, 404, "user not found"); return; }
      res.set_content(UserToJson(*u).dump(), "application/json");
   });

   //--- POST /api/users  body: { username, password, role }
   srv.Post("/api/users", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      const std::string username = j.value("username", std::string());
      const std::string password = j.value("password", std::string());
      const std::string role     = j.value("role",     std::string("viewer"));
      const bool        active   = j.value("active",   true);
      std::string verr;
      if(!ValidateUsername(username, &verr)) { SendError(res, 400, verr); return; }
      if(!ValidatePassword(password, &verr)) { SendError(res, 400, verr); return; }
      if(!ValidRole(role))                   { SendError(res, 400, "role must be 'admin' or 'viewer'"); return; }
      if(UserRepo::GetByUsername(*ctx->db, username)) { SendError(res, 409, "username already exists"); return; }

      User u;
      u.username      = username;
      u.password_hash = Crypto::HashPassword(password);
      u.role          = role;
      u.active        = active;
      if(u.password_hash.empty()) { SendError(res, 500, "hash failed"); return; }
      UserRepo::Insert(*ctx->db, u);
      const auto admin = CurrentUser::User();
      ctx->log->Info("User created: '%s' role=%s by '%s'",
                     u.username.c_str(), u.role.c_str(),
                     admin ? admin->username.c_str() : "?");
      res.status = 201;
      res.set_content(UserToJson(u).dump(), "application/json");
   });

   //--- PATCH /api/users/:id  body: { role?, active? }
   srv.Patch(R"(/api/users/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = UserRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "user not found"); return; }
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string role   = j.value("role",   cur->role);
      bool        active = j.value("active", cur->active);
      if(!ValidRole(role)) { SendError(res, 400, "role must be 'admin' or 'viewer'"); return; }

      //--- Last-admin protection: refuse to demote/deactivate the only active admin.
      const bool was_admin = (cur->role == "admin" && cur->active);
      const bool will_admin = (role == "admin" && active);
      if(was_admin && !will_admin && UserRepo::CountActiveAdmins(*ctx->db) <= 1)
      { SendError(res, 409, "cannot demote/deactivate the last active admin"); return; }

      UserRepo::UpdateRoleActive(*ctx->db, id, role, active);
      //--- If the user was deactivated, drop their sessions immediately.
      if(!active) ctx->sessions->RevokeUser(id);

      auto updated = UserRepo::Get(*ctx->db, id);
      if(!updated) { SendError(res, 500, "post-update fetch failed"); return; }
      const auto admin = CurrentUser::User();
      ctx->log->Info("User #%lld updated: role=%s active=%d by '%s'",
                     (long long)id, role.c_str(), active ? 1 : 0,
                     admin ? admin->username.c_str() : "?");
      res.set_content(UserToJson(*updated).dump(), "application/json");
   });

   //--- PATCH /api/users/:id/password  body: { new_password }
   srv.Patch(R"(/api/users/(\d+)/password)", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = UserRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "user not found"); return; }
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      const std::string new_pw = j.value("new_password", std::string());
      std::string verr;
      if(!ValidatePassword(new_pw, &verr)) { SendError(res, 400, verr); return; }
      const std::string new_hash = Crypto::HashPassword(new_pw);
      if(new_hash.empty()) { SendError(res, 500, "hash failed"); return; }
      UserRepo::UpdatePassword(*ctx->db, id, new_hash);
      ctx->sessions->RevokeUser(id);
      const auto admin = CurrentUser::User();
      ctx->log->Info("Admin reset password for user #%lld by '%s'",
                     (long long)id, admin ? admin->username.c_str() : "?");
      res.set_content(R"({"ok":true})", "application/json");
   });

   //--- DELETE /api/users/:id
   srv.Delete(R"(/api/users/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      const auto admin = CurrentUser::User();
      if(admin && admin->id == id) { SendError(res, 409, "cannot delete yourself"); return; }
      auto cur = UserRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "user not found"); return; }
      if(cur->role == "admin" && cur->active && UserRepo::CountActiveAdmins(*ctx->db) <= 1)
      { SendError(res, 409, "cannot delete the last active admin"); return; }
      UserRepo::Delete(*ctx->db, id);
      ctx->log->Info("User #%lld ('%s') deleted by '%s'",
                     (long long)id, cur->username.c_str(),
                     admin ? admin->username.c_str() : "?");
      res.set_content(R"({"deleted":true})", "application/json");
   });
}
