//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Scheduler.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Scheduler.h"
#include "TelegramClient.h"
#include "Crypto.h"
#include "TimeUtil.h"
#include "../api/AppContext.h"
#include "../api/JobRunner.h"
#include "../db/Repos.h"
#include "../reports/Expression.h"
#include "../reports/FieldCatalog.h"

using nlohmann::json;

Scheduler::Scheduler(AppContext* ctx) : m_ctx(ctx) {}
Scheduler::~Scheduler() { Stop(); }

void Scheduler::Start()
{
   if(m_th.joinable()) return;
   m_stop = false;
   m_th = std::thread([this]{ Loop(); });
}

void Scheduler::Stop()
{
   { std::lock_guard<std::mutex> lk(m_mu); m_stop = true; }
   m_cv.notify_all();
   if(m_th.joinable()) m_th.join();
}

void Scheduler::Loop()
{
   Logger::SetRequestId("scheduler");
   m_ctx->log->Info("Scheduler thread started.");
   while(!m_stop)
   {
      try { TickOnce(); }
      catch(const std::exception& e) { m_ctx->log->Error("Scheduler tick: %s", e.what()); }

      std::unique_lock<std::mutex> lk(m_mu);
      m_cv.wait_for(lk, std::chrono::seconds(60), [this]{ return m_stop.load(); });
   }
   m_ctx->log->Info("Scheduler thread exiting.");
   Logger::SetRequestId("");
}

namespace
{
   //--- Scheduling and relative-date math is done in broker UTC (the MT5 broker
   //--- we connect to uses UTC trading-day boundaries — see TimeUtil.cpp). The
   //--- offset constant is kept (== 0 now) so the helper structure below stays
   //--- intact and can be re-pointed if a future broker uses a different TZ.
   constexpr int64_t kTzOffsetSec = 0;

   //--- Internal: gmtime/_mkgmtime helpers operate on UTC. To work in "local
   //--- wall clock" (GMT+3) we shift by +offset before reading, and -offset
   //--- before persisting.

   int DowUtc(int64_t unix_secs)
   {
      time_t t = (time_t)unix_secs;
      struct tm utc{};
      gmtime_s(&utc, &t);
      return utc.tm_wday;
   }

   int64_t MakeUtcRaw(int year, int month_1, int day, int hour, int minute)
   {
      struct tm utc{};
      utc.tm_year = year - 1900;
      utc.tm_mon  = month_1 - 1;
      utc.tm_mday = day;
      utc.tm_hour = hour;
      utc.tm_min  = minute;
      utc.tm_sec  = 0;
      return (int64_t)_mkgmtime(&utc);
   }

   void ToYmdUtc(int64_t unix_secs, int* year, int* mon, int* mday)
   {
      time_t t = (time_t)unix_secs;
      struct tm utc{}; gmtime_s(&utc, &t);
      *year = utc.tm_year + 1900;
      *mon  = utc.tm_mon  + 1;
      *mday = utc.tm_mday;
   }

   //--- Local-wall (GMT+3) variants used by the scheduler.

   //--- Y/M/D of the GMT+3 calendar day that contains the given UTC instant.
   void ToYmdLocal(int64_t now_utc, int* y, int* m, int* d)
   {
      ToYmdUtc(now_utc + kTzOffsetSec, y, m, d);
   }

   //--- UTC unix timestamp of a GMT+3 wall-clock y/m/d hh:mm.
   int64_t MakeUtcFromLocal(int year, int month_1, int day, int hour, int minute)
   {
      return MakeUtcRaw(year, month_1, day, hour, minute) - kTzOffsetSec;
   }

   //--- Day-of-week of the GMT+3 calendar day (0=Sun..6=Sat).
   int DowLocal(int64_t unix_secs)
   {
      return DowUtc(unix_secs + kTzOffsetSec);
   }

   //--- Y/M/D of the GMT+3 day reached by adding delta calendar days to now.
   void AddDaysLocal(int64_t now_utc, int delta_days, int* y, int* m, int* d)
   {
      ToYmdUtc(now_utc + kTzOffsetSec + (int64_t)delta_days * 86400, y, m, d);
   }

   std::string YmdString(int year, int mon, int mday)
   {
      char buf[16];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, mon, mday);
      return buf;
   }

   //--- Compute (from_ymd, to_ymd) for relative presets.
   //--- All "day" boundaries are GMT+3; "today" = current GMT+3 date.
   void ResolveRelative(const std::string& preset, int relative_n, int64_t now,
                        std::string* from, std::string* to)
   {
      int y, mo, d;
      ToYmdLocal(now, &y, &mo, &d);
      if(preset == "today")
      {
         *from = YmdString(y, mo, d);
         *to   = *from;
      }
      else if(preset == "yesterday")
      {
         int y2,mo2,d2; AddDaysLocal(now, -1, &y2, &mo2, &d2);
         *from = YmdString(y2, mo2, d2);
         *to   = *from;
      }
      else if(preset == "last_n_days")
      {
         const int n = relative_n > 0 ? relative_n : 7;
         int y1,mo1,d1; AddDaysLocal(now, -n,  &y1, &mo1, &d1);
         int y2,mo2,d2; AddDaysLocal(now, -1,  &y2, &mo2, &d2);
         *from = YmdString(y1, mo1, d1);
         *to   = YmdString(y2, mo2, d2);
      }
      else if(preset == "this_week")
      {
         //--- ISO week starts Monday. dow: 0=Sun..6=Sat → adjust.
         const int dow = DowLocal(now);
         const int back = (dow == 0) ? 6 : (dow - 1);   // Sunday → 6 days back
         int y1,mo1,d1; AddDaysLocal(now, -back, &y1, &mo1, &d1);
         *from = YmdString(y1, mo1, d1);
         *to   = YmdString(y, mo, d);
      }
      else if(preset == "last_week")
      {
         const int dow = DowLocal(now);
         const int back = (dow == 0) ? 6 : (dow - 1);
         int y1,mo1,d1; AddDaysLocal(now, -back - 7, &y1, &mo1, &d1);
         int y2,mo2,d2; AddDaysLocal(now, -back - 1, &y2, &mo2, &d2);
         *from = YmdString(y1, mo1, d1);
         *to   = YmdString(y2, mo2, d2);
      }
      else if(preset == "this_month")
      {
         *from = YmdString(y, mo, 1);
         *to   = YmdString(y, mo, d);
      }
      else if(preset == "last_month")
      {
         int ly = y, lm = mo - 1;
         if(lm == 0) { lm = 12; ly--; }
         //--- last day of previous month = (1st of current month, GMT+3) − 1 day
         const int64_t first_of_month_utc = MakeUtcFromLocal(y, mo, 1, 0, 0);
         int ldy, ldm, ldd;
         AddDaysLocal(first_of_month_utc, -1, &ldy, &ldm, &ldd);
         *from = YmdString(ly, lm, 1);
         *to   = YmdString(ldy, ldm, ldd);
      }
      else
      {
         //--- unknown preset — default to last 7 days
         int y1,mo1,d1; AddDaysLocal(now, -7, &y1, &mo1, &d1);
         int y2,mo2,d2; AddDaysLocal(now, -1, &y2, &mo2, &d2);
         *from = YmdString(y1, mo1, d1);
         *to   = YmdString(y2, mo2, d2);
      }
   }
}

int64_t Scheduler::ComputeNext(const ScheduleEntry& s, int64_t now)
{
   //--- All wall-clock math in broker UTC (the connected MT5 broker uses UTC
   //--- trading-day boundaries — kTzOffsetSec is 0). next_run_at is stored UTC.
   int y, mo, d; ToYmdLocal(now, &y, &mo, &d);
   if(s.frequency == "hourly")
   {
      const int n = std::max(1, s.every_n_hours);
      const int64_t step = (int64_t)n * 3600;
      //--- Snap to (s.time_minute) past the next n-hour boundary, where
      //--- boundaries align to GMT+3 midnight.
      int64_t base = MakeUtcFromLocal(y, mo, d, 0, s.time_minute);
      while(base <= now) base += step;
      return base;
   }
   if(s.frequency == "daily")
   {
      int64_t cand = MakeUtcFromLocal(y, mo, d, s.time_hour, s.time_minute);
      if(cand <= now)
      {
         int y2,mo2,d2; AddDaysLocal(now, 1, &y2, &mo2, &d2);
         cand = MakeUtcFromLocal(y2, mo2, d2, s.time_hour, s.time_minute);
      }
      return cand;
   }
   if(s.frequency == "weekly")
   {
      const int target_dow = s.day_of_week % 7;
      for(int add = 0; add < 14; ++add)
      {
         int y2,mo2,d2; AddDaysLocal(now, add, &y2, &mo2, &d2);
         const int64_t cand = MakeUtcFromLocal(y2, mo2, d2, s.time_hour, s.time_minute);
         if(DowLocal(cand) != target_dow) continue;
         if(cand > now) return cand;
      }
      return now + 7 * 86400;
   }
   if(s.frequency == "monthly")
   {
      int dom = std::max(1, std::min(28, s.day_of_month));
      for(int month_offset = 0; month_offset < 3; ++month_offset)
      {
         int ty = y, tm = mo + month_offset;
         while(tm > 12) { tm -= 12; ty++; }
         const int64_t cand = MakeUtcFromLocal(ty, tm, dom, s.time_hour, s.time_minute);
         if(cand > now) return cand;
      }
      return now + 30 * 86400;
   }
   //--- default: 1 hour from now
   return now + 3600;
}

json Scheduler::BuildRunParams(const ReadyMadeReport& rm,
                                const std::vector<std::string>& template_date_params,
                                int64_t now)
{
   //--- Resolve date map: param_name → "YYYY-MM-DD"
   json dates = json::object();
   if(rm.date_strategy == "fixed")
   {
      json fixed = json::parse(rm.fixed_dates_json, nullptr, false);
      if(fixed.is_object())
         for(auto it = fixed.begin(); it != fixed.end(); ++it)
            if(it.value().is_string())
               dates[it.key()] = it.value().get<std::string>();
   }
   else
   {
      //--- relative — produce from/to and map to first two date params.
      std::string from_s, to_s;
      ResolveRelative(rm.relative_preset.empty() ? "last_n_days" : rm.relative_preset,
                      rm.relative_n, now, &from_s, &to_s);

      //--- Prefer canonical names if present.
      bool has_from = false, has_to = false;
      for(const auto& p : template_date_params)
      {
         if(p == "date_from") { dates[p] = from_s; has_from = true; }
         else if(p == "date_to") { dates[p] = to_s; has_to = true; }
      }
      if(!has_from || !has_to)
      {
         //--- Fall back to positional first/second.
         if(!template_date_params.empty())     dates[template_date_params[0]] = from_s;
         if(template_date_params.size() >= 2)  dates[template_date_params[1]] = to_s;
      }
   }

   json out = {
      { "template_id", rm.template_id },
      { "dates",       dates },
   };
   if(rm.account_filter_id) out["account_filter_id"] = rm.account_filter_id;
   if(rm.top_n_override)    out["top_n"]             = rm.top_n_override;
   return out;
}

namespace
{
   //--- Helper: load template's date_params (needed to map relative→names).
   std::vector<std::string> LoadTemplateDateParams(SqliteDb& db, int64_t template_id)
   {
      auto t = TemplateRepo::Get(db, template_id);
      if(!t) return {};
      return t->date_params;
   }

   //--- Decrypt + read telegram bot token (empty if not configured).
   std::string LoadBotToken(SqliteDb& db)
   {
      const std::string enc = SettingsRepo::Get(db, "telegram_bot_token_encrypted");
      if(enc.empty()) return "";
      std::string plain;
      if(!Crypto::DecryptB64(enc, &plain)) return "";
      return plain;
   }
}

void Scheduler::TickOnce()
{
   const int64_t now = (int64_t)time(nullptr);

   //--- 1. Pending delivery sweep: for each dispatched schedule, check job status.
   for(auto& sch : ScheduleRepo::ListDispatched(*m_ctx->db))
   {
      if(!sch.last_job_id) continue;
      auto job = JobRepo::Get(*m_ctx->db, sch.last_job_id);
      if(!job) { ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed", "job missing"); continue; }
      if(job->status == JobStatus::Queued || job->status == JobStatus::Running) continue;

      if(job->status == JobStatus::Failed)
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed",
            "job failed: " + job->error_message);
         m_ctx->log->Warn("Schedule %lld: job %lld failed (%s)",
                          (long long)sch.id, (long long)job->id, job->error_message.c_str());
         continue;
      }

      //--- Completed: deliver to Telegram.
      const std::string token = LoadBotToken(*m_ctx->db);
      if(token.empty())
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed", "telegram bot token not configured");
         continue;
      }
      std::string chat = sch.telegram_chat_id;
      if(chat.empty()) chat = SettingsRepo::Get(*m_ctx->db, "telegram_default_chat_id");
      if(chat.empty())
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed", "no chat_id (set per-schedule or global default)");
         continue;
      }

      std::string caption = sch.name + " — job #" + std::to_string(job->id);
      if(job->csv_filename.empty())
      {
         //--- No CSV — send a summary message only.
         auto r = TelegramClient::SendMessage(token, chat, caption + " (no CSV)");
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, r.ok ? "completed" : "failed", r.error);
         continue;
      }

      const std::string csv_path = job->output_dir + "/" + job->csv_filename;
      const auto r = TelegramClient::SendDocument(token, chat, csv_path,
                                                  job->csv_filename, caption);
      ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, r.ok ? "completed" : "failed", r.error);
      m_ctx->log->Info("Schedule %lld delivery: %s (chat=%s)",
                       (long long)sch.id, r.ok ? "ok" : r.error.c_str(), chat.c_str());
   }

   //--- 2. Due dispatch: for each enabled schedule whose next_run_at <= now,
   //--- build & enqueue a new job. Catch-up safety: if next_run_at is more
   //--- than 24h behind, skip to fresh next firing without dispatching.
   for(auto& sch : ScheduleRepo::ListDue(*m_ctx->db, now))
   {
      auto rm = ReadyMadeRepo::Get(*m_ctx->db, sch.ready_made_id);
      if(!rm)
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed", "ready-made missing");
         continue;
      }
      if(now - sch.next_run_at > 24 * 3600 && sch.next_run_at > 0)
      {
         //--- More than a day late — recompute without firing.
         const int64_t nxt = ComputeNext(sch, now);
         ScheduleRepo::UpdateDispatch(*m_ctx->db, sch.id, sch.last_run_at, nxt, sch.last_job_id);
         m_ctx->log->Warn("Schedule %lld: stale (>24h late), skipping firing, next=%lld",
                          (long long)sch.id, (long long)nxt);
         continue;
      }

      auto tpl = TemplateRepo::Get(*m_ctx->db, rm->template_id);
      if(!tpl)
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed", "template missing");
         continue;
      }

      //--- Need a manager. Use the bound account filter's manager_id, else fail.
      int64_t manager_id = 0;
      if(rm->account_filter_id)
      {
         if(auto af = AccountFilterRepo::Get(*m_ctx->db, rm->account_filter_id))
            manager_id = af->manager_id;
      }
      if(!manager_id)
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed",
            "no manager_id (bind a manager-scoped account filter on the ready-made)");
         continue;
      }

      json params;
      try { params = BuildRunParams(*rm, tpl->date_params, now); }
      catch(const std::exception& e)
      {
         ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, "failed",
            std::string("build run params: ") + e.what());
         continue;
      }
      params["manager_id"] = manager_id;

      JobRow row;
      row.template_id       = rm->template_id;
      row.manager_id        = manager_id;
      row.account_filter_id = rm->account_filter_id;
      row.params_json       = params.dump();
      JobRepo::Create(*m_ctx->db, row);
      m_ctx->jobs->Enqueue(row.id);

      const int64_t nxt = ComputeNext(sch, now);
      ScheduleRepo::UpdateDispatch(*m_ctx->db, sch.id, now, nxt, row.id);
      m_ctx->log->Info("Schedule %lld dispatched job %lld; next=%lld",
                       (long long)sch.id, (long long)row.id, (long long)nxt);
   }
}
