//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     AppContext.h - shared services bag passed to routes/jobs    |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Logger.h"
#include "../core/ThreadPool.h"
#include "../db/SqliteDb.h"
#include "../mt5/ConnectionPool.h"
#include <memory>
#include <string>

class JobRunner;

struct ServerConfig
{
   std::string  host           = "0.0.0.0";
   int          port           = 5151;
   std::string  db_path        = "data/reportTool.sqlite";
   std::string  output_dir     = "data/output";
   std::string  log_path       = "data/run.log";
   std::string  dll_dir        = ".";              // where MT5APIManager64.dll lives (next to exe)
   int          thread_pool_size = 8;
   int          max_concurrent_jobs = 2;
};

struct AppContext
{
   ServerConfig                       cfg;
   std::shared_ptr<Logger>            log;
   std::shared_ptr<SqliteDb>          db;
   std::shared_ptr<ConnectionPool>    pool;
   std::shared_ptr<ThreadPool>        threads;
   std::shared_ptr<JobRunner>         jobs;
};
