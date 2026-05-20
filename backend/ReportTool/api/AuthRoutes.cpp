//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       AuthRoutes.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "AuthRoutes.h"
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

   void SetSessionCookie(httplib::Response& res, const std::string& token)
   {
      //--- HttpOnly + SameSite=Lax + Path=/. We do NOT set Secure because the
      //--- typical deployment is internal HTTP behind nginx; the operator can
      //--- terminate TLS at the proxy and add the Secure attribute there.
      char buf[512];
      snprintf(buf, sizeof(buf),
               "session=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%lld",
               token.c_str(), (long long)SessionManager::kSessionTtlSec);
      res.set_header("Set-Cookie", buf);
   }

   void ClearSessionCookie(httplib::Response& res)
   {
      res.set_header("Set-Cookie",
         "session=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
   }

   //--- Lightweight password sanity check (length only — internal tool).
   bool ValidatePassword(const std::string& pw, std::string* err)
   {
      if(pw.size() < 6)  { *err = "password must be at least 6 characters"; return false; }
      if(pw.size() > 256){ *err = "password too long";                       return false; }
      return true;
   }

   bool ValidateUsername(const std::string& u, std::string* err)
   {
      if(u.empty())      { *err = "username required";          return false; }
      if(u.size() > 64)  { *err = "username too long";          return false; }
      for(char c : u)
         if(!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-'))
         { *err = "username may only contain letters, digits, _ . -"; return false; }
      return true;
   }
}

void AuthRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   //--- GET /api/auth/setup-status -- public; tells the UI whether to show
   //--- the first-run setup wizard or the login page.
   srv.Get("/api/auth/setup-status", [ctx](const httplib::Request&, httplib::Response& res){
      const bool needs = UserRepo::Count(*ctx->db) == 0;
      res.set_content(json{ {"needs_setup", needs} }.dump(), "application/json");
   });

   //--- POST /api/auth/setup -- creates the first admin only when the users
   //--- table is empty. Race-safe: re-check count inside the same call.
   srv.Post("/api/auth/setup", [ctx](const httplib::Request& req, httplib::Response& res){
      if(UserRepo::Count(*ctx->db) > 0) { SendError(res, 409, "setup already complete"); return; }
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      const std::string username = j.value("username", std::string());
      const std::string password = j.value("password", std::string());
      std::string verr;
      if(!ValidateUsername(username, &verr)) { SendError(res, 400, verr); return; }
      if(!ValidatePassword(password, &verr)) { SendError(res, 400, verr); return; }

      //--- Recheck inside the critical section (Count + Insert).
      if(UserRepo::Count(*ctx->db) > 0) { SendError(res, 409, "setup already complete"); return; }

      User u;
      u.username      = username;
      u.password_hash = Crypto::HashPassword(password);
      u.role          = "admin";
      u.active        = true;
      if(u.password_hash.empty()) { SendError(res, 500, "hash failed"); return; }
      UserRepo::Insert(*ctx->db, u);
      const int64_t now = (int64_t)time(nullptr);
      UserRepo::UpdateLastActive(*ctx->db, u.id, now);
      u.last_active_at = now;

      const std::string token = ctx->sessions->IssueSession(
         u.id, req.remote_addr, req.get_header_value("User-Agent"));
      if(token.empty()) { SendError(res, 500, "session issue failed"); return; }
      SetSessionCookie(res, token);
      ctx->log->Info("Setup complete: first admin '%s' created", u.username.c_str());
      res.set_content(json{ {"user", UserToJson(u)} }.dump(), "application/json");
   });

   //--- POST /api/auth/login -- public.
   srv.Post("/api/auth/login", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      const std::string username = j.value("username", std::string());
      const std::string password = j.value("password", std::string());
      if(username.empty() || password.empty())
      { SendError(res, 400, "username and password required"); return; }

      auto u = UserRepo::GetByUsername(*ctx->db, username);
      //--- Identical client-facing error messages to avoid leaking user
      //--- existence; verbose diagnostics go to the server log only.
      if(!u)
      {
         ctx->log->Warn("Auth login failed: unknown user '%s' from %s",
                        username.c_str(), req.remote_addr.c_str());
         SendError(res, 401, "invalid credentials");
         return;
      }
      if(!u->active)
      {
         ctx->log->Warn("Auth login failed: user '%s' is inactive (from %s)",
                        username.c_str(), req.remote_addr.c_str());
         SendError(res, 401, "invalid credentials");
         return;
      }
      if(!Crypto::VerifyPassword(password, u->password_hash))
      {
         ctx->log->Warn("Auth login failed: password mismatch for '%s' from %s",
                        username.c_str(), req.remote_addr.c_str());
         SendError(res, 401, "invalid credentials");
         return;
      }
      const int64_t now = (int64_t)time(nullptr);
      UserRepo::UpdateLastActive(*ctx->db, u->id, now);
      u->last_active_at = now;
      const std::string token = ctx->sessions->IssueSession(
         u->id, req.remote_addr, req.get_header_value("User-Agent"));
      if(token.empty()) { SendError(res, 500, "session issue failed"); return; }
      SetSessionCookie(res, token);
      ctx->log->Info("Auth login OK: user='%s' from=%s", u->username.c_str(), req.remote_addr.c_str());
      res.set_content(json{ {"user", UserToJson(*u)} }.dump(), "application/json");
   });

   //--- POST /api/auth/logout -- authenticated.
   srv.Post("/api/auth/logout", [ctx](const httplib::Request&, httplib::Response& res){
      const auto tok = CurrentUser::SessionToken();
      if(tok) ctx->sessions->Invalidate(*tok);
      ClearSessionCookie(res);
      res.status = 204;
   });

   //--- GET /api/auth/me -- authenticated.
   srv.Get("/api/auth/me", [](const httplib::Request&, httplib::Response& res){
      const auto u = CurrentUser::User();
      if(!u) { SendError(res, 401, "not authenticated"); return; }
      res.set_content(json{ {"user", UserToJson(*u)} }.dump(), "application/json");
   });

   //--- POST /api/auth/change-password -- authenticated, self-service.
   //--- Drops all OTHER sessions for the user; keeps the caller's cookie alive.
   srv.Post("/api/auth/change-password", [ctx](const httplib::Request& req, httplib::Response& res){
      const auto cu = CurrentUser::User();
      const auto ct = CurrentUser::SessionToken();
      if(!cu || !ct) { SendError(res, 401, "not authenticated"); return; }
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      const std::string old_pw = j.value("old_password", std::string());
      const std::string new_pw = j.value("new_password", std::string());
      std::string verr;
      if(!ValidatePassword(new_pw, &verr)) { SendError(res, 400, verr); return; }
      //--- Re-fetch the user to read the current password_hash.
      auto u = UserRepo::Get(*ctx->db, cu->id);
      if(!u) { SendError(res, 404, "user not found"); return; }
      if(!Crypto::VerifyPassword(old_pw, u->password_hash))
      { SendError(res, 401, "old password incorrect"); return; }
      const std::string new_hash = Crypto::HashPassword(new_pw);
      if(new_hash.empty()) { SendError(res, 500, "hash failed"); return; }
      UserRepo::UpdatePassword(*ctx->db, cu->id, new_hash);
      ctx->sessions->RevokeUserExcept(cu->id, *ct);
      ctx->log->Info("Auth: user '%s' changed own password", cu->username.c_str());
      res.set_content(R"({"ok":true})", "application/json");
   });
}
