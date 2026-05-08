//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                            main.cpp - server bootstrap           |
//+------------------------------------------------------------------+
#include "stdafx.h"
#include "core/Logger.h"
#include "core/Crypto.h"
#include "db/SqliteDb.h"
#include "db/Schema.h"
#include "db/Repos.h"
#include "mt5/Connection.h"
#include "mt5/ConnectionPool.h"
#include "core/ThreadPool.h"
#include "api/AppContext.h"
#include "api/HttpServer.h"
#include "api/JobRunner.h"
#include "third_party/httplib.h"
#include <filesystem>
#include <fstream>
#include <signal.h>

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
   ServerConfig LoadConfig(const std::string& path)
   {
      ServerConfig c;
      std::ifstream f(path);
      if(!f) return c;
      json j = json::parse(f, nullptr, false);
      if(j.is_discarded()) return c;
      c.host                = j.value("host", c.host);
      c.port                = j.value("port", c.port);
      c.db_path             = j.value("db_path", c.db_path);
      c.output_dir          = j.value("output_dir", c.output_dir);
      c.log_path            = j.value("log_path", c.log_path);
      c.dll_dir             = j.value("dll_dir", c.dll_dir);
      c.thread_pool_size    = j.value("thread_pool_size", c.thread_pool_size);
      c.max_concurrent_jobs = j.value("max_concurrent_jobs", c.max_concurrent_jobs);
      return c;
   }

   HttpServer* g_server = nullptr;

   BOOL WINAPI CtrlHandler(DWORD type)
   {
      if(type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT)
      {
         if(g_server) g_server->Stop();
         return TRUE;
      }
      return FALSE;
   }
}

int main(int argc, char** argv)
{
   const std::string cfg_path = (argc > 1) ? argv[1] : "config/server.json";
   ServerConfig cfg = LoadConfig(cfg_path);

   std::error_code ec;
   fs::create_directories(fs::path(cfg.log_path).parent_path(), ec);
   fs::create_directories(cfg.output_dir, ec);
   fs::create_directories(fs::path(cfg.db_path).parent_path(), ec);

   auto log = std::make_shared<Logger>(cfg.log_path);
   log->Banner("MT5 ReportTool starting");
   log->Info("config: %s", cfg_path.c_str());
   log->Info("db_path: %s", cfg.db_path.c_str());
   log->Info("output_dir: %s", cfg.output_dir.c_str());

   //--- Master key
   {
      char buf[128]; size_t n = 0;
      if(getenv_s(&n, buf, sizeof(buf), "REPORTTOOL_MASTER_KEY") != 0 || n == 0)
      {
         log->Error("REPORTTOOL_MASTER_KEY not set; aborting (manager passwords cannot be encrypted/decrypted)");
         return 2;
      }
      std::string hex(buf, (n > 0 && buf[n - 1] == 0) ? n - 1 : n);
      auto key = Crypto::HexToKey(hex);
      if(key.empty())
      {
         log->Error("REPORTTOOL_MASTER_KEY must be 64 hex chars (256-bit key)");
         return 2;
      }
      std::string err;
      if(!Crypto::Init(key, &err))
      {
         log->Error("Crypto init failed: %s", err.c_str());
         return 2;
      }
   }

   //--- DB
   auto db = std::make_shared<SqliteDb>();
   {
      std::string err;
      if(!db->Open(cfg.db_path, &err)) { log->Error("Open SQLite: %s", err.c_str()); return 3; }
      if(!Schema::Apply(*db, &err))    { log->Error("Schema: %s", err.c_str()); return 3; }
      JobRepo::MarkInterruptedAsFailed(*db);
   }

   //--- MT5 SDK factory
   if(!Connection::InitFactory(cfg.dll_dir, *log)) return 4;

   //--- Pool / threads / jobs
   auto pool    = std::make_shared<ConnectionPool>(*log);
   auto threads = std::make_shared<ThreadPool>((size_t)cfg.thread_pool_size);

   AppContext ctx;
   ctx.cfg     = cfg;
   ctx.log     = log;
   ctx.db      = db;
   ctx.pool    = pool;
   ctx.threads = threads;
   ctx.jobs    = std::make_shared<JobRunner>(&ctx);
   ctx.jobs->Start();

   //--- HTTP server
   HttpServer srv(&ctx);
   g_server = &srv;
   SetConsoleCtrlHandler(CtrlHandler, TRUE);
   srv.Listen();

   log->Info("Shutting down ...");
   ctx.jobs->Stop();
   pool->Clear();
   Connection::ShutdownFactory();
   Crypto::Shutdown();
   return 0;
}
