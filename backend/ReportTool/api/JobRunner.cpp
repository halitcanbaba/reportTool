//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       JobRunner.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "JobRunner.h"
#include "AppContext.h"
#include "../db/Repos.h"
#include "../reports/Engine.h"

JobRunner::JobRunner(AppContext* ctx) : m_ctx(ctx) {}
JobRunner::~JobRunner() { Stop(); }

void JobRunner::Start()
{
   if(m_th.joinable()) return;
   m_th = std::thread([this]{ Loop(); });
}

void JobRunner::Stop()
{
   { std::lock_guard<std::mutex> lk(m_mu); m_stop = true; }
   m_cv.notify_all();
   if(m_th.joinable()) m_th.join();
}

void JobRunner::Enqueue(int64_t job_id)
{
   { std::lock_guard<std::mutex> lk(m_mu); m_q.push(job_id); }
   m_cv.notify_one();
}

void JobRunner::Loop()
{
   for(;;)
   {
      int64_t id = 0;
      {
         std::unique_lock<std::mutex> lk(m_mu);
         m_cv.wait(lk, [this]{ return m_stop || !m_q.empty(); });
         if(m_stop && m_q.empty()) return;
         id = m_q.front(); m_q.pop();
      }
      try { RunOne(id); }
      catch(const std::exception& e)
      {
         m_ctx->log->Error("JobRunner exception job=%lld: %s", (long long)id, e.what());
         JobRepo::UpdateStatus(*m_ctx->db, id, JobStatus::Failed, 1.0, e.what());
      }
   }
}

void JobRunner::RunOne(int64_t job_id)
{
   Logger::SetRequestId(std::string("job:") + std::to_string(job_id));
   m_running++;
   try
   {
      Engine::Run(*m_ctx, job_id);
   }
   catch(...)
   {
      m_running--;
      Logger::SetRequestId("");
      throw;
   }
   m_running--;
   Logger::SetRequestId("");
}
