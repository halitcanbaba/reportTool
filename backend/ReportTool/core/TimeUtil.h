//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                  TimeUtil.h - UTC time and calendar helpers      |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace TimeUtil
{
   int64_t  DateStringToTime(const std::string& date_str);   // "YYYY-MM-DD" -> UTC midnight unix s
   int64_t  ParseMonth(const std::string& yyyy_mm,
                       int64_t* out_to_exclusive = nullptr); // "YYYY-MM" -> first day, sets out_to to next month
   std::string FormatDateTime(int64_t t);
   std::string FormatDate(int64_t t);
   std::string MonthLabel(int64_t t);
   std::string RunStamp();

   int64_t  UtcMidnight(int64_t t);                          // floor to UTC midnight
   void     IterateDays(int64_t from, int64_t to_exclusive,
                        std::function<void(int64_t)> fn);
}
