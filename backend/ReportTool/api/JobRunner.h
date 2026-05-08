//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     JobRunner.h - async report-job dispatcher                   |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>

struct AppContext;

class JobRunner
{
public:
   explicit JobRunner(AppContext* ctx);
   ~JobRunner();

   void Start();
   void Stop();

   //--- enqueue an existing job row (assumes JobRepo::Create already ran)
   void Enqueue(int64_t job_id);

private:
   AppContext*               m_ctx;
   std::thread               m_th;
   std::queue<int64_t>       m_q;
   std::mutex                m_mu;
   std::condition_variable   m_cv;
   std::atomic<bool>         m_stop{false};
   std::atomic<int>          m_running{0};

   void Loop();
   void RunOne(int64_t job_id);
};
