//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       HttpServer.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "HttpServer.h"
#include "../third_party/httplib.h"
#include "../core/SessionManager.h"
#include "../db/Repos.h"
#include "CurrentUser.h"
#include "ManagerRoutes.h"
#include "ReportRoutes.h"
#include "HealthRoutes.h"
#include "TemplateRoutes.h"
#include "AccountFilterRoutes.h"
#include "BlueprintRoutes.h"
#include "ReadyMadeRoutes.h"
#include "ScheduleRoutes.h"
#include "SettingsRoutes.h"
#include "AuthRoutes.h"
#include "UserRoutes.h"
#include "FolderRoutes.h"

using nlohmann::json;

namespace
{
   //--- Parse the value of the "session" cookie out of a Cookie header.
   //--- Cookie format: "name1=value1; name2=value2; ..."
   std::string ExtractSessionCookie(const std::string& header)
   {
      const std::string key = "session=";
      size_t i = 0;
      while(i < header.size())
      {
         //--- Skip spaces / semicolons.
         while(i < header.size() && (header[i] == ' ' || header[i] == ';')) ++i;
         //--- Find end of this pair.
         size_t end = i;
         while(end < header.size() && header[end] != ';') ++end;
         //--- Compare prefix.
         if(end - i > key.size() && header.compare(i, key.size(), key) == 0)
            return header.substr(i + key.size(), end - i - key.size());
         i = end;
      }
      return "";
   }

   //--- Public routes — no auth needed.
   bool IsPublicPath(const httplib::Request& req)
   {
      if(req.method == "OPTIONS")                return true;
      if(req.path == "/health")                  return true;
      if(req.path == "/api/auth/setup-status")   return true;
      if(req.path == "/api/auth/setup")          return true;
      if(req.path == "/api/auth/login")          return true;
      return false;
   }

   //--- Path/method combinations that require role=admin.
   //--- Viewer accounts get a 403 here.
   bool RequiresAdmin(const httplib::Request& req)
   {
      const std::string& m = req.method;
      const std::string& p = req.path;

      //--- All user management is admin-only.
      if(p.rfind("/api/users", 0) == 0)                       return true;
      //--- Settings (Telegram token, etc.) — admin only.
      if(p.rfind("/api/settings", 0) == 0)                    return true;
      //--- Running reports / scheduler triggers — admin only.
      if(m == "POST" && p == "/api/reports/run")              return true;
      if(m == "POST" && p.find("/run-now") != std::string::npos)  return true;
      if(m == "POST" && p.find("/api/ready-made/") == 0
                     && p.find("/run") != std::string::npos)  return true;
      //--- Any write on first-class entities.
      if(m == "POST" || m == "PATCH" || m == "DELETE" || m == "PUT")
      {
         //--- Auth self-service stays viewer-allowed.
         if(p.rfind("/api/auth/", 0) == 0)                    return false;
         //--- Validation is read-flavored; allow viewers.
         if(p == "/api/templates/validate")                   return false;
         //--- Account filter preview is a read on MT5 data via POST body — viewer OK.
         if(p == "/api/account-filters/preview")              return false;
         //--- Everything else writing is admin.
         if(p.rfind("/api/", 0) == 0)                         return true;
      }
      return false;
   }

   void Reply401(httplib::Response& res, const char* msg)
   {
      res.status = 401;
      res.set_content(json{ {"error", msg}, {"code", "unauthenticated"} }.dump(),
                       "application/json");
   }

   void Reply403(httplib::Response& res)
   {
      res.status = 403;
      res.set_content(json{ {"error", "admin role required"}, {"code", "forbidden"} }.dump(),
                       "application/json");
   }
}

HttpServer::HttpServer(AppContext* ctx)
   : m_ctx(ctx), m_srv(std::make_unique<httplib::Server>())
{
   //--- httplib defaults to an 8 MB body cap. We accept large multipart uploads
   //--- (≤ 50 MB Telegram sendDocument cap) on /api/reports/jobs/:id/send-telegram,
   //--- so raise the limit to 60 MB for headroom.
   m_srv->set_payload_max_length(60ull * 1024 * 1024);
   RegisterCors();
   RegisterAuthMiddleware();
   RegisterRoutes();
}

HttpServer::~HttpServer() { Stop(); }

void HttpServer::RegisterCors()
{
   m_srv->set_default_headers({
      { "Access-Control-Allow-Origin",  "*" },
      { "Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS" },
      { "Access-Control-Allow-Headers", "Content-Type, Authorization" },
   });

   m_srv->Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
      res.status = 204;
   });
}

void HttpServer::RegisterAuthMiddleware()
{
   AppContext* ctx = m_ctx;
   m_srv->set_pre_routing_handler(
      [ctx](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse
      {
         CurrentUser::Clear();
         if(IsPublicPath(req)) return httplib::Server::HandlerResponse::Unhandled;

         const std::string cookie = req.get_header_value("Cookie");
         const std::string token  = ExtractSessionCookie(cookie);
         auto pair = ctx->sessions ? ctx->sessions->ValidateAndExtend(token)
                                    : std::optional<std::pair<User,Session>>{};
         if(!pair) { Reply401(res, "login required"); return httplib::Server::HandlerResponse::Handled; }

         //--- Stash for route handlers.
         CurrentUser::Set(pair->first, pair->second.token);

         if(RequiresAdmin(req) && pair->first.role != "admin")
         {
            Reply403(res);
            return httplib::Server::HandlerResponse::Handled;
         }
         return httplib::Server::HandlerResponse::Unhandled;
      });

   m_srv->set_post_routing_handler(
      [](const httplib::Request&, httplib::Response&) {
         CurrentUser::Clear();
      });
}

void HttpServer::RegisterRoutes()
{
   HealthRoutes        ::Register(*m_srv, m_ctx);
   AuthRoutes          ::Register(*m_srv, m_ctx);
   UserRoutes          ::Register(*m_srv, m_ctx);
   ManagerRoutes       ::Register(*m_srv, m_ctx);
   AccountFilterRoutes ::Register(*m_srv, m_ctx);
   BlueprintRoutes     ::Register(*m_srv, m_ctx);
   TemplateRoutes      ::Register(*m_srv, m_ctx);
   ReadyMadeRoutes     ::Register(*m_srv, m_ctx);
   ScheduleRoutes      ::Register(*m_srv, m_ctx);
   SettingsRoutes      ::Register(*m_srv, m_ctx);
   FolderRoutes        ::Register(*m_srv, m_ctx);
   ReportRoutes        ::Register(*m_srv, m_ctx);
}

bool HttpServer::Listen()
{
   m_running = true;
   m_ctx->log->Info("HTTP listening on %s:%d", m_ctx->cfg.host.c_str(), m_ctx->cfg.port);
   const bool ok = m_srv->listen(m_ctx->cfg.host.c_str(), m_ctx->cfg.port);
   m_running = false;
   return ok;
}

void HttpServer::Stop()
{
   if(m_running) m_srv->stop();
}
