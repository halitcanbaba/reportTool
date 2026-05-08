//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       JobRunner.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "JobRunner.h"
#include "AppContext.h"
#include "../db/Repos.h"
#include "../mt5/DataLoader.h"
#include "../core/RegexCache.h"
#include "../core/TimeUtil.h"
#include "../reports/TopWinnerReport.h"
#include "../reports/SummaryReport.h"
#include "../reports/ReportWriter.h"
#include <filesystem>

using nlohmann::json;
namespace fs = std::filesystem;

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

namespace
{
   ManagerRow LoadManagerOrThrow(SqliteDb& db, int64_t mgr_id)
   {
      auto m = ManagerRepo::Get(db, mgr_id);
      if(!m) throw std::runtime_error("manager not found");
      return *m;
   }

   void EnsureDir(const std::string& path)
   {
      std::error_code ec;
      fs::create_directories(path, ec);
   }
}

void JobRunner::RunOne(int64_t job_id)
{
   Logger::SetRequestId(std::string("job:") + std::to_string(job_id));
   m_running++;
   auto job_opt = JobRepo::Get(*m_ctx->db, job_id);
   if(!job_opt) { m_running--; Logger::SetRequestId(""); return; }
   JobRow job = *job_opt;

   JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.05);

   ManagerRow mgr = LoadManagerOrThrow(*m_ctx->db, job.manager_id);

   //--- compile filters
   CompiledFilters filters; std::string err;
   if(!CompiledFilters::Compile(mgr.regex_filters, &filters, &err))
      throw std::runtime_error("regex: " + err);

   //--- connect
   auto conn = m_ctx->pool->GetOrConnect(mgr);
   if(!conn) throw std::runtime_error("connection failed");

   //--- parse params
   json params = json::parse(job.params_json, nullptr, false);
   if(params.is_discarded()) throw std::runtime_error("bad params_json");

   //--- output dir per job
   std::string job_dir = m_ctx->cfg.output_dir + "/job_" + std::to_string(job_id);
   EnsureDir(job_dir);

   if(job.kind == ReportKind::TopWinner)
   {
      const std::string df = params.value("date_from", "");
      const std::string dt = params.value("date_to",   "");
      const uint32_t  topn = params.value("top_n",      20u);
      if(df.empty()) throw std::runtime_error("date_from is empty or missing (YYYY-MM-DD)");
      if(dt.empty()) throw std::runtime_error("date_to is empty or missing (YYYY-MM-DD)");
      if(topn == 0)  throw std::runtime_error("top_n must be >= 1");

      const int64_t from = TimeUtil::DateStringToTime(df);
      const int64_t to   = TimeUtil::DateStringToTime(dt) + 86400; // inclusive day -> exclusive
      if(from >= to) throw std::runtime_error("date_from must be <= date_to");

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.10);
      auto users = DataLoader::LoadUsers(*conn, mgr.group_masks, mgr.group_regex,
                                         mgr.login_min, mgr.login_max, *m_ctx->log);
      std::vector<uint64_t> logins; logins.reserve(users.size());
      for(const auto& u : users) logins.push_back(u.login);

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.25);
      auto deals = DataLoader::LoadDealsParallel(*conn, *m_ctx->threads, logins, from, to, *m_ctx->log);

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.65);
      const std::vector<int64_t> targets_open  = { from - 86400 };
      const std::vector<int64_t> targets_close = { to   - 86400 };
      auto bnd_open  = DataLoader::LoadDailyBoundary(*conn, *m_ctx->threads, logins, targets_open,  7, *m_ctx->log);
      auto bnd_close = DataLoader::LoadDailyBoundary(*conn, *m_ctx->threads, logins, targets_close, 7, *m_ctx->log);

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.85);
      auto result = TopWinnerReport::Build(mgr, filters, users, deals, bnd_open, bnd_close, from, to, topn);

      const std::string csv = ReportWriter::WriteTopWinnerCsv(result, job_dir, job_id);
      const std::string preview = ReportWriter::TopWinnerToJson(result);
      JobRepo::UpdateOutput(*m_ctx->db, job_id, job_dir, csv, "", preview);
   }
   else  // Summary
   {
      int64_t from = 0, to = 0;
      if(params.contains("month") && !params["month"].get<std::string>().empty())
      {
         from = TimeUtil::ParseMonth(params["month"], &to);
      }
      else
      {
         const std::string df = params.value("date_from", "");
         const std::string dt = params.value("date_to",   "");
         if(df.empty()) throw std::runtime_error("date_from required when month not provided");
         if(dt.empty()) throw std::runtime_error("date_to required when month not provided");
         from = TimeUtil::DateStringToTime(df);
         to   = TimeUtil::DateStringToTime(dt) + 86400;
      }
      if(from >= to) throw std::runtime_error("date_from must be <= date_to");

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.10);
      auto users = DataLoader::LoadUsers(*conn, mgr.group_masks, mgr.group_regex,
                                         mgr.login_min, mgr.login_max, *m_ctx->log);
      std::vector<uint64_t> logins; logins.reserve(users.size());
      for(const auto& u : users) logins.push_back(u.login);

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.30);
      auto deals = DataLoader::LoadDealsParallel(*conn, *m_ctx->threads, logins, from, to, *m_ctx->log);

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.65);
      //--- daily covers [from-86400, to] so we can compute first-day floating change
      auto daily = DataLoader::LoadDailyParallel(*conn, *m_ctx->threads, logins, from - 86400, to, *m_ctx->log);

      JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Running, 0.85);
      auto result = SummaryReport::Build(mgr, filters, users, deals, daily, from, to);

      const std::string csv = ReportWriter::WriteSummaryCsv(result, job_dir, job_id);
      const std::string preview = ReportWriter::SummaryToJson(result);
      JobRepo::UpdateOutput(*m_ctx->db, job_id, job_dir, csv, "", preview);
   }

   JobRepo::UpdateStatus(*m_ctx->db, job_id, JobStatus::Completed, 1.0);
   m_ctx->log->Info("job %lld completed", (long long)job_id);
   m_running--;
   Logger::SetRequestId("");
}
