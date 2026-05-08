//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     HttpServer.h - cpp-httplib bootstrap + route registration   |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
#include <atomic>
#include <thread>
#include <memory>

namespace httplib { class Server; }

class HttpServer
{
public:
   explicit HttpServer(AppContext* ctx);
   ~HttpServer();

   bool Listen();    // blocks; returns when Stop() is called
   void Stop();

private:
   AppContext*                       m_ctx;
   std::unique_ptr<httplib::Server>  m_srv;
   std::atomic<bool>                 m_running{false};

   void RegisterCors();
   void RegisterRoutes();
};
