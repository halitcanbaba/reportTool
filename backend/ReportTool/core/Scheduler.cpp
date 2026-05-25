//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Scheduler.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Scheduler.h"
#include "TelegramClient.h"
#include "XlsxWriter.h"
#include "PdfWriter.h"
#include "ChromeRenderer.h"
#include "Crypto.h"
#include "TimeUtil.h"
#include "../api/AppContext.h"
#include "../api/JobRunner.h"
#include "../db/Repos.h"
#include "../reports/Expression.h"
#include "../reports/FieldCatalog.h"
#include <fstream>
#include <sstream>
#include <random>
#include <cstdlib>
#include <windows.h>

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

   //--- Recover stale claims on startup. A row stuck in 'delivering' means
   //--- a previous process crashed mid-Telegram-upload — reset to
   //--- 'dispatched' so the first tick can retry. There's no live tick to
   //--- race here (we haven't entered the loop yet).
   try {
      m_ctx->db->Exec("UPDATE schedules SET last_status='dispatched' "
                      "WHERE last_status='delivering'", nullptr);
   } catch(...) { /* non-fatal */ }

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
   //--- Schedule clock — UTC+3 (Istanbul / GMT+3, no DST). All time_hour /
   //--- time_minute / day_of_week / day_of_month inputs are interpreted in
   //--- this local frame; next_run_at is still persisted as a UTC unix
   //--- timestamp so cross-tz consistency is preserved. NOTE: TimeUtil's
   //--- own kTzOffsetSec stays at 0 — date_params (YYYY-MM-DD) and MT5
   //--- daily snapshot boundaries remain UTC, which matches what the
   //--- engine actually reads from the broker.
   constexpr int64_t kTzOffsetSec = 3 * 3600;

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

   //--- Last calendar day of (year, month_1). Handles Feb leap-year via the
   //--- standard rule (divisible by 4, not 100, unless 400). Lets monthly
   //--- schedules with day_of_month=31 actually fire on Apr 30, Feb 28/29.
   int LastDayOfMonth(int year, int month_1)
   {
      static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
      if(month_1 < 1 || month_1 > 12) return 28;
      int d = days[month_1 - 1];
      if(month_1 == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) d = 29;
      return d;
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
         //--- Sealed days only: `to` = yesterday so equity_end-based formulas
         //--- read a complete daily snapshot. For "include today" pick
         //--- `this_week_to_date` below.
         const int dow = DowLocal(now);
         const int back = (dow == 0) ? 6 : (dow - 1);   // Sunday → 6 days back
         int y1,mo1,d1; AddDaysLocal(now, -back, &y1, &mo1, &d1);
         int y2,mo2,d2; AddDaysLocal(now, -1,    &y2, &mo2, &d2);
         *from = YmdString(y1, mo1, d1);
         *to   = YmdString(y2, mo2, d2);
      }
      else if(preset == "this_week_to_date")
      {
         //--- Includes today; partial daily snapshot. Use when explicitly
         //--- comparing intraday running totals.
         const int dow = DowLocal(now);
         const int back = (dow == 0) ? 6 : (dow - 1);
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
         //--- Sealed days only: `to` = yesterday. Same rationale as this_week.
         int y2,mo2,d2; AddDaysLocal(now, -1, &y2, &mo2, &d2);
         *from = YmdString(y, mo, 1);
         *to   = YmdString(y2, mo2, d2);
      }
      else if(preset == "this_month_to_date")
      {
         //--- Includes today; partial snapshot. Daily-snapshot formulas like
         //--- equity_end(date_to) will silently fall back to yesterday's row
         //--- since today's hasn't been written yet — opt-in only.
         *from = YmdString(y, mo, 1);
         *to   = YmdString(y, mo, d);
      }
      else if(preset == "last_n_days_to_date")
      {
         //--- Includes today (today−n+1 .. today). Mirror of last_n_days but
         //--- with the trailing edge bumped one day forward.
         const int n = relative_n > 0 ? relative_n : 7;
         int y1,mo1,d1; AddDaysLocal(now, -(n - 1), &y1, &mo1, &d1);
         *from = YmdString(y1, mo1, d1);
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

   //--- True when the given UTC instant's local day-of-week is allowed by the
   //--- schedule's `days_of_week` filter. Empty filter = every day.
   auto dowAllowed = [&](int64_t ts) {
      if(s.days_of_week.empty()) return true;
      const int dow = DowLocal(ts);
      for(int x : s.days_of_week) if(x == dow) return true;
      return false;
   };

   if(s.frequency == "hourly")
   {
      //--- Pick the hour set: either the user-specified subset, or the legacy
      //--- every-N-hours sweep (when `hours` is empty).
      std::vector<int> hour_set = s.hours;
      if(hour_set.empty())
      {
         const int n = std::max(1, s.every_n_hours);
         for(int h = 0; h < 24; h += n) hour_set.push_back(h);
      }
      //--- Walk forward day-by-day; for each allowed weekday try every hour in
      //--- sorted order. Cap at 14 days so a degenerate config can't loop.
      for(int day_offset = 0; day_offset < 14; ++day_offset)
      {
         int yy, mm, dd; AddDaysLocal(now, day_offset, &yy, &mm, &dd);
         const int64_t day_anchor = MakeUtcFromLocal(yy, mm, dd, 0, 0);
         if(!dowAllowed(day_anchor)) continue;
         for(int h : hour_set)
         {
            const int64_t cand = MakeUtcFromLocal(yy, mm, dd, h, s.time_minute);
            if(cand > now) return cand;
         }
      }
      //--- Defensive fallback: nothing fit in 14 days (shouldn't happen).
      return now + 3600;
   }
   if(s.frequency == "daily")
   {
      //--- Walk forward up to 30 days searching for an allowed weekday.
      for(int day_offset = 0; day_offset < 30; ++day_offset)
      {
         int yy, mm, dd; AddDaysLocal(now, day_offset, &yy, &mm, &dd);
         const int64_t cand = MakeUtcFromLocal(yy, mm, dd, s.time_hour, s.time_minute);
         if(!dowAllowed(cand)) continue;
         if(cand > now) return cand;
      }
      return now + 86400;
   }
   if(s.frequency == "weekly")
   {
      //--- Prefer the days_of_week[] array (multi-day weekly support) and
      //--- fall back to the legacy single day_of_week when it's empty so
      //--- schedules saved before this change keep their behaviour.
      std::vector<int> targets;
      if(!s.days_of_week.empty())
      {
         for(int x : s.days_of_week) targets.push_back(((x % 7) + 7) % 7);
      }
      else
      {
         targets.push_back(((s.day_of_week % 7) + 7) % 7);
      }
      for(int add = 0; add < 14; ++add)
      {
         int y2,mo2,d2; AddDaysLocal(now, add, &y2, &mo2, &d2);
         const int64_t cand = MakeUtcFromLocal(y2, mo2, d2, s.time_hour, s.time_minute);
         const int cand_dow = DowLocal(cand);
         bool ok = false;
         for(int t : targets) if(t == cand_dow) { ok = true; break; }
         if(!ok) continue;
         if(cand > now) return cand;
      }
      return now + 7 * 86400;
   }
   if(s.frequency == "monthly")
   {
      const int requested = std::max(1, s.day_of_month);
      for(int month_offset = 0; month_offset < 3; ++month_offset)
      {
         int ty = y, tm = mo + month_offset;
         while(tm > 12) { tm -= 12; ty++; }
         //--- Clamp to the actual length of the candidate month so e.g.
         //--- day_of_month=31 falls to Apr 30 / Feb 28-29 instead of the
         //--- previous hard 28-day cap that silently truncated user intent.
         const int dom = std::min(requested, LastDayOfMonth(ty, tm));
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
   if(rm.deposit_filter_id) out["deposit_filter_id"] = rm.deposit_filter_id;
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

   //--- Read full file into memory; returns empty string on error.
   //--- Used for the XLSX path which needs to parse the job's CSV row by row.
   std::string ReadFile(const std::string& path)
   {
      std::ifstream f(path, std::ios::binary);
      if(!f) return "";
      std::ostringstream ss; ss << f.rdbuf();
      return ss.str();
   }

   //--- Minimal RFC4180 CSV row parser. Handles quoted fields with embedded
   //--- commas / newlines / "" escaped quotes. Stateful across calls because
   //--- a quoted field may span newlines.
   struct CsvParser {
      const std::string& src;
      size_t pos = 0;
      bool   eof() const { return pos >= src.size(); }

      //--- Parse one record into `out`; returns false at EOF before any data.
      //--- Empty trailing row (final \n) is treated as EOF.
      bool NextRow(std::vector<std::string>& out)
      {
         out.clear();
         if(eof()) return false;
         std::string field;
         bool in_quotes = false;
         bool consumed_any = false;
         while(pos < src.size())
         {
            const char c = src[pos++];
            consumed_any = true;
            if(in_quotes)
            {
               if(c == '"')
               {
                  if(pos < src.size() && src[pos] == '"') { field += '"'; ++pos; }
                  else                                    { in_quotes = false; }
               }
               else field += c;
            }
            else
            {
               if(c == ',') { out.push_back(std::move(field)); field.clear(); }
               else if(c == '\r')
               {
                  if(pos < src.size() && src[pos] == '\n') ++pos;
                  out.push_back(std::move(field));
                  return true;
               }
               else if(c == '\n')
               {
                  out.push_back(std::move(field));
                  return true;
               }
               else if(c == '"' && field.empty()) { in_quotes = true; }
               else field += c;
            }
         }
         if(consumed_any) { out.push_back(std::move(field)); return true; }
         return false;
      }
   };

   //--- Build an .xlsx blob from the job's persisted CSV. Reuses the
   //--- template's per-column format hints to type cells (numeric vs
   //--- string) — login / count / money / pct columns all land as numbers
   //--- so Excel can sort and aggregate; text/date stay as strings.
   std::string BuildXlsxFromJob(SqliteDb& db, const JobRow& job,
                                 const std::string& sheet_name)
   {
      const std::string csv_path = job.output_dir + "/" + job.csv_filename;
      const std::string csv_text = ReadFile(csv_path);
      if(csv_text.empty()) return "";

      //--- Pull column formats from the template; fall back to plain strings
      //--- if the template was deleted between job creation and delivery.
      std::vector<ColumnSpec::Format> col_formats;
      auto tpl = TemplateRepo::Get(db, job.template_id);
      if(tpl) {
         for(const auto& c : tpl->columns) col_formats.push_back(c.format);
      }

      auto is_numeric_format = [](ColumnSpec::Format f) {
         return f == ColumnSpec::Format::Money
             || f == ColumnSpec::Format::Int
             || f == ColumnSpec::Format::Pct
             || f == ColumnSpec::Format::Number;
      };

      CsvParser p{ csv_text };
      std::vector<std::string> first;
      if(!p.NextRow(first)) return "";

      //--- Use template labels as headers when available — the CSV header
      //--- row uses backend keys (col_1, …) which aren't user-friendly.
      std::vector<std::string> headers;
      if(tpl && !tpl->columns.empty()) {
         for(const auto& c : tpl->columns) headers.push_back(c.label);
      } else {
         headers = first;
      }

      std::vector<std::vector<XlsxWriter::Cell>> rows;
      std::vector<std::string> raw;
      while(p.NextRow(raw))
      {
         std::vector<XlsxWriter::Cell> row;
         row.reserve(raw.size());
         for(size_t i = 0; i < raw.size(); ++i)
         {
            XlsxWriter::Cell cell;
            cell.text = raw[i];
            //--- Numeric typing: column.format says money/int/pct/number.
            //--- Identifier-int (login) is also numeric — Excel renders as
            //--- a plain number with no thousand separators.
            const bool is_num = i < col_formats.size() && is_numeric_format(col_formats[i]);
            if(is_num && !cell.text.empty())
            {
               //--- Validate parseability so a stray string doesn't break
               //--- the spreadsheet (Excel rejects sheet1.xml with a
               //--- non-numeric value inside <v>).
               char* end = nullptr;
               (void)std::strtod(cell.text.c_str(), &end);
               cell.is_number = (end && *end == '\0');
            }
            row.push_back(std::move(cell));
         }
         rows.push_back(std::move(row));
      }

      return XlsxWriter::Build(sheet_name, headers, rows);
   }

   //--- PDF variant: same CSV → header + data rows, but as plain strings
   //--- (the PdfWriter doesn't type cells — it just monospace-prints).
   //--- Reuses the same template-aware header substitution so columns
   //--- display by label rather than raw key.
   std::string BuildPdfFromJob(SqliteDb& db, const JobRow& job,
                                const std::string& title)
   {
      const std::string csv_path = job.output_dir + "/" + job.csv_filename;
      const std::string csv_text = ReadFile(csv_path);
      if(csv_text.empty()) return "";

      auto tpl = TemplateRepo::Get(db, job.template_id);

      CsvParser p{ csv_text };
      std::vector<std::string> first;
      if(!p.NextRow(first)) return "";

      std::vector<std::string> headers;
      if(tpl && !tpl->columns.empty()) {
         for(const auto& c : tpl->columns) headers.push_back(c.label);
      } else {
         headers = first;
      }

      std::vector<std::vector<std::string>> rows;
      std::vector<std::string> raw;
      while(p.NextRow(raw)) rows.push_back(std::move(raw));

      return PdfWriter::Build(title, headers, rows);
   }

   //--- Return the screenshot bypass token, generating one on first call.
   //--- Stored under SettingsRepo so it survives restarts and so the
   //--- pre-routing auth handler can validate it without coordination.
   std::string EnsureScreenshotToken(SqliteDb& db)
   {
      std::string tok = SettingsRepo::Get(db, "screenshot_token");
      if(!tok.empty()) return tok;
      //--- 32 hex chars from a non-deterministic source. random_device
      //--- gives at least OS-grade entropy on Windows (CryptGenRandom).
      std::random_device rd;
      static const char* hex = "0123456789abcdef";
      tok.reserve(32);
      for(int i = 0; i < 32; ++i) tok += hex[rd() & 0xF];
      SettingsRepo::Set(db, "screenshot_token", tok);
      return tok;
   }

   //--- Read a file's bytes into memory. Returns empty on failure.
   std::string ReadPngFile(const std::string& path)
   {
      std::ifstream f(path, std::ios::binary);
      if(!f) return "";
      std::ostringstream ss; ss << f.rdbuf();
      return ss.str();
   }

   //--- Build a screenshot of the job result page via headless Chrome.
   //--- Returns empty bytes on any failure (chrome not installed, render
   //--- timed out, page didn't load, …); caller falls back to text.
   std::string BuildScreenshotForJob(SqliteDb& db, const JobRow& job)
   {
      if(!ChromeRenderer::Available()) return "";

      //--- Bootstrap URL: backend (nginx-fronted) endpoint that sets the
      //--- short-lived screenshot cookie then 302s to the result page.
      //--- screenshot_url_base is the public origin the headless browser
      //--- should hit — typically http://localhost:<nginx_port>. Defaults
      //--- to 8090 (the project's nginx convention).
      std::string base = SettingsRepo::Get(db, "screenshot_url_base");
      if(base.empty()) base = "http://localhost:8090";
      const std::string tok = EnsureScreenshotToken(db);
      //--- ?_render=table strips the sidebar / breadcrumbs / action buttons
      //--- so the screenshot focuses on the result table — see
      //--- Layout.tsx + ResultViewPage.tsx for the chrome-stripping branch.
      const std::string path = "/jobs/" + std::to_string(job.id) + "?_render=table";

      //--- URL-encode the redirect path (just /).
      auto urlenc = [](const std::string& s) {
         std::string out; out.reserve(s.size() * 3);
         for(unsigned char c : s) {
            if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/')
               out += (char)c;
            else { char buf[4]; std::snprintf(buf, sizeof(buf), "%%%02X", c); out += buf; }
         }
         return out;
      };

      const std::string url = base + "/api/auth/screenshot-bootstrap?token=" + tok
                            + "&path=" + urlenc(path);

      //--- Output to user's temp dir so we don't clutter the install dir.
      char tmp_path[MAX_PATH];
      if(!GetTempPathA(MAX_PATH, tmp_path)) return "";
      const std::string out_path = std::string(tmp_path) + "rt_job_"
                                   + std::to_string(job.id) + ".png";

      if(!ChromeRenderer::RenderPng(url, out_path, 1600, 1024, 30)) return "";
      std::string png = ReadPngFile(out_path);
      _unlink(out_path.c_str());
      return png;
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

      //--- Atomic claim: flip "dispatched" -> "delivering" so a slow
      //--- delivery (large xlsx, slow Telegram upload) can't be picked up
      //--- a second time by the next tick if it overruns 60s. Loser sees
      //--- 0 rows changed and skips silently — winner owns the delivery.
      if(!ScheduleRepo::ClaimStatus(*m_ctx->db, sch.id, "dispatched", "delivering"))
      {
         continue;
      }

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

      //--- Catch-up tag: if the dispatch flagged this run as late (server
      //--- was down past 24h), prepend "[catchup +Nh]" so the recipient
      //--- knows the data isn't a fresh on-time run.
      int64_t catchup_hours = 0;
      if(!job->params_json.empty()) {
         auto pj = nlohmann::json::parse(job->params_json, nullptr, false);
         if(!pj.is_discarded() && pj.contains("_catchup_hours_late")
                                && pj["_catchup_hours_late"].is_number_integer()) {
            catchup_hours = pj["_catchup_hours_late"].get<int64_t>();
         }
      }
      std::string caption = sch.name + " — job #" + std::to_string(job->id);
      if(catchup_hours > 0)
         caption = "[catchup +" + std::to_string(catchup_hours) + "h] " + caption;
      const std::string fmt = sch.delivery_format.empty() ? std::string("csv")
                                                          : sch.delivery_format;

      TelegramClient::Result r;
      if(fmt == "text")
      {
         //--- Telegram HTML mode: schedule name bolded; KPI block in <pre>
         //--- so numbers line up monospaced. Escape user-supplied text
         //--- (schedule name, date strings) to avoid &/</> breaking the
         //--- parse_mode=HTML payload.
         auto esc = [](const std::string& s){
            std::string out; out.reserve(s.size());
            for(char c : s) {
               if     (c == '&') out += "&amp;";
               else if(c == '<') out += "&lt;";
               else if(c == '>') out += "&gt;";
               else              out += c;
            }
            return out;
         };

         std::string text = "<b>" + esc(sch.name) + "</b>"
                          + "  <i>job #" + std::to_string(job->id) + "</i>";

         //--- Resolve date range and basic counts from summary + params.
         int total_logins = 0, row_count = 0;
         std::string date_from, date_to;
         if(!job->summary_json.empty())
         {
            auto j = nlohmann::json::parse(job->summary_json, nullptr, false);
            if(!j.is_discarded()) {
               total_logins = j.value("total_logins", 0);
               row_count    = j.value("row_count",    0);
            }
         }
         if(!job->params_json.empty())
         {
            auto p = nlohmann::json::parse(job->params_json, nullptr, false);
            if(!p.is_discarded() && p.contains("dates") && p["dates"].is_object())
            {
               if(p["dates"].contains("date_from") && p["dates"]["date_from"].is_string())
                  date_from = p["dates"]["date_from"].get<std::string>();
               if(p["dates"].contains("date_to") && p["dates"]["date_to"].is_string())
                  date_to = p["dates"]["date_to"].get<std::string>();
            }
         }

         text += "\n<pre>";
         if(!date_from.empty())
         {
            text += "Period   " + esc(date_from);
            if(!date_to.empty() && date_to != date_from) text += "  →  " + esc(date_to);
            text += "\n";
         }
         text += "Rows     " + std::to_string(row_count) + "\n";
         text += "Logins   " + std::to_string(total_logins);
         text += "</pre>";

         //--- Telegram caps at 4096 chars (counting tags). Stay well under.
         if(text.size() > 3800) text = text.substr(0, 3800) + "…";
         r = TelegramClient::SendMessage(token, chat, text, "HTML");
      }
      else if(fmt == "xlsx")
      {
         //--- Built in-memory from the persisted CSV using XlsxWriter.
         //--- Telegram caps documents at 50 MB; our store-mode zip can
         //--- inflate a few-MB CSV to a few-MB xlsx — comfortably under.
         const std::string blob = BuildXlsxFromJob(*m_ctx->db, *job, sch.name);
         if(blob.empty())
         {
            r = TelegramClient::SendMessage(token, chat, caption + " (xlsx build failed)");
         }
         else
         {
            const std::string fname = sch.name + ".xlsx";
            const std::string mime  = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
            r = TelegramClient::SendDocumentBytes(token, chat, blob, fname, mime, caption);
         }
      }
      else if(fmt == "pdf")
      {
         const std::string blob = BuildPdfFromJob(*m_ctx->db, *job, sch.name);
         if(blob.empty())
         {
            r = TelegramClient::SendMessage(token, chat, caption + " (pdf build failed)");
         }
         else
         {
            const std::string fname = sch.name + ".pdf";
            r = TelegramClient::SendDocumentBytes(token, chat, blob, fname, "application/pdf", caption);
         }
      }
      else if(fmt == "image")
      {
         //--- Headless Chrome renders the actual result-page SPA, so the
         //--- screenshot matches what the user sees in their browser.
         //--- Falls back to a text notice when Chrome isn't installed or
         //--- the render times out — Telegram still gets a heads-up.
         const std::string png = BuildScreenshotForJob(*m_ctx->db, *job);
         if(png.empty())
         {
            r = TelegramClient::SendMessage(token, chat, caption + " (screenshot unavailable)");
         }
         else
         {
            const std::string fname = sch.name + ".png";
            r = TelegramClient::SendPhotoBytes(token, chat, png, fname, caption);
         }
      }
      else if(job->csv_filename.empty())
      {
         //--- csv requested but no file produced — fall back to text.
         r = TelegramClient::SendMessage(token, chat, caption + " (no CSV)");
      }
      else
      {
         //--- Default `csv` path: attach the CSV file.
         const std::string csv_path = job->output_dir + "/" + job->csv_filename;
         r = TelegramClient::SendDocument(token, chat, csv_path,
                                           job->csv_filename, caption);
      }
      ScheduleRepo::UpdateDelivery(*m_ctx->db, sch.id, r.ok ? "completed" : "failed", r.error);
      m_ctx->log->Info("Schedule %lld delivery [%s]: %s (chat=%s)",
                       (long long)sch.id, fmt.c_str(),
                       r.ok ? "ok" : r.error.c_str(), chat.c_str());
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
      //--- Catch-up: when a schedule is significantly late (server down,
      //--- power outage, …) we still fire one run so the user notices the
      //--- gap and gets the most recent data. The dispatched job carries a
      //--- "[catchup N h late]" tag in its params so the delivery caption
      //--- can surface that a backfill is happening rather than silently
      //--- skipping. Previously we just advanced next_run_at and dropped
      //--- the firing — a silent data-loss footgun across restarts.
      const bool is_catchup = sch.next_run_at > 0
                              && (now - sch.next_run_at) > 24 * 3600;
      if(is_catchup)
      {
         m_ctx->log->Warn("Schedule %lld: %lldh late, firing catch-up run",
                          (long long)sch.id,
                          (long long)((now - sch.next_run_at) / 3600));
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
      //--- Embed lateness so the delivery handler can prefix the Telegram
      //--- caption with [catchup +Nh]. Surfaces server-downtime gaps to
      //--- the recipient instead of letting them assume the report is
      //--- fresh.
      if(is_catchup) params["_catchup_hours_late"] = (now - sch.next_run_at) / 3600;

      JobRow row;
      row.template_id       = rm->template_id;
      row.manager_id        = manager_id;
      row.account_filter_id = rm->account_filter_id;
      row.params_json       = params.dump();
      //--- Single SQLite transaction so Create + dispatch state move
      //--- atomically. Without this, a crash between the two leaves a
      //--- schedule with next_run_at still in the past + last_status='',
      //--- which would re-dispatch on restart (duplicate Telegram send).
      const int64_t nxt = ComputeNext(sch, now);
      try {
         m_ctx->db->Exec("BEGIN", nullptr);
         JobRepo::Create(*m_ctx->db, row);
         ScheduleRepo::UpdateDispatch(*m_ctx->db, sch.id, now, nxt, row.id);
         m_ctx->db->Exec("COMMIT", nullptr);
      }
      catch(...) {
         m_ctx->db->Exec("ROLLBACK", nullptr);
         m_ctx->log->Error("Schedule %lld: dispatch tx failed", (long long)sch.id);
         continue;
      }
      //--- Enqueue runs outside the tx — JobRunner picks up queued rows
      //--- from the table directly so this is just a wakeup hint.
      m_ctx->jobs->Enqueue(row.id);
      m_ctx->log->Info("Schedule %lld dispatched job %lld; next=%lld",
                       (long long)sch.id, (long long)row.id, (long long)nxt);
   }
}
