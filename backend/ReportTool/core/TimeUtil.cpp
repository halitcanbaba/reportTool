//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       TimeUtil.cpp                |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "TimeUtil.h"

namespace
{
   //--- The whole tool runs in MT5-server-local time (GMT+3). Date strings
   //--- entered by the user / produced by reports are interpreted in this
   //--- timezone; we convert to UTC unix only at the storage / SDK boundary.
   constexpr int64_t kTzOffsetSec = 3 * 3600;

   int64_t MakeUtc(int year, int month, int day)
   {
      struct tm t = {};
      t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
      //--- "YYYY-MM-DD 00:00 GMT+3" → UTC unix
      return (int64_t)_mkgmtime(&t) - kTzOffsetSec;
   }
}

int64_t TimeUtil::DateStringToTime(const std::string& s)
{
   int y = 2020, m = 1, d = 1;
   sscanf_s(s.c_str(), "%d-%d-%d", &y, &m, &d);
   return MakeUtc(y, m, d);
}

int64_t TimeUtil::ParseMonth(const std::string& s, int64_t* out_to_exclusive)
{
   int y = 2020, m = 1;
   sscanf_s(s.c_str(), "%d-%d", &y, &m);
   int64_t from = MakeUtc(y, m, 1);
   int ny = (m == 12) ? y + 1 : y;
   int nm = (m == 12) ? 1     : m + 1;
   if(out_to_exclusive) *out_to_exclusive = MakeUtc(ny, nm, 1);
   return from;
}

std::string TimeUtil::FormatDateTime(int64_t t)
{
   //--- Display all timestamps in GMT+3 to match MT5 server clock.
   time_t raw = (time_t)(t + kTzOffsetSec); struct tm tm_info; gmtime_s(&tm_info, &raw);
   char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
   return buf;
}

std::string TimeUtil::FormatDate(int64_t t)
{
   time_t raw = (time_t)(t + kTzOffsetSec); struct tm tm_info; gmtime_s(&tm_info, &raw);
   char buf[16]; strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_info);
   return buf;
}

std::string TimeUtil::MonthLabel(int64_t t)
{
   time_t raw = (time_t)t; struct tm tm_info; gmtime_s(&tm_info, &raw);
   char buf[16]; strftime(buf, sizeof(buf), "%Y-%m", &tm_info);
   return buf;
}

std::string TimeUtil::RunStamp()
{
   time_t t = time(nullptr); struct tm tm_info; localtime_s(&tm_info, &t);
   char buf[32]; strftime(buf, sizeof(buf), "run_%Y%m%d_%H%M%S", &tm_info);
   return buf;
}

int64_t TimeUtil::UtcMidnight(int64_t t)
{
   return t - (t % 86400);
}

void TimeUtil::IterateDays(int64_t from, int64_t to_exclusive, std::function<void(int64_t)> fn)
{
   for(int64_t d = UtcMidnight(from); d < to_exclusive; d += 86400) fn(d);
}
