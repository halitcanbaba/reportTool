//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       HttpServer.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "HttpServer.h"
#include "../third_party/httplib.h"
#include "ManagerRoutes.h"
#include "ReportRoutes.h"
#include "HealthRoutes.h"

HttpServer::HttpServer(AppContext* ctx)
   : m_ctx(ctx), m_srv(std::make_unique<httplib::Server>())
{
   RegisterCors();
   RegisterRoutes();
}

HttpServer::~HttpServer() { Stop(); }

void HttpServer::RegisterCors()
{
   m_srv->set_default_headers({
      { "Access-Control-Allow-Origin",  "*" },
      { "Access-Control-Allow-Methods", "GET,POST,PATCH,DELETE,OPTIONS" },
      { "Access-Control-Allow-Headers", "Content-Type, Authorization" },
   });

   m_srv->Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) {
      res.status = 204;
   });
}

void HttpServer::RegisterRoutes()
{
   HealthRoutes  ::Register(*m_srv, m_ctx);
   ManagerRoutes ::Register(*m_srv, m_ctx);
   ReportRoutes  ::Register(*m_srv, m_ctx);
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
