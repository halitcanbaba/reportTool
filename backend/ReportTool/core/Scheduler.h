//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     Scheduler.h - background thread for ready-made schedules     |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../third_party/json.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

struct AppContext;

class Scheduler
{
public:
   explicit Scheduler(AppContext* ctx);
   ~Scheduler();

   void Start();
   void Stop();

   //--- Compute the next firing time after `now` for the given schedule.
   //--- Pure helper exposed for testing + route preview.
   static int64_t ComputeNext(const ScheduleEntry& s, int64_t now);

   //--- Build a /api/reports/run-style params_json for the given ready-made,
   //--- resolving the date strategy against `now`. Throws on invalid inputs.
   static nlohmann::json BuildRunParams(const ReadyMadeReport& rm,
                                         const std::vector<std::string>& template_date_params,
                                         int64_t now);

private:
   AppContext*               m_ctx;
   std::thread               m_th;
   std::atomic<bool>         m_stop{false};
   std::condition_variable   m_cv;
   std::mutex                m_mu;

   void Loop();
   void TickOnce();
};
