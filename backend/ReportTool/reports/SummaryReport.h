//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     SummaryReport.h - daily metrics + monthly aggregate          |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../core/RegexCache.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace SummaryReport
{
   struct Result
   {
      std::string                  header;          // "{brand} Monthly Figures"
      SummaryMetrics               metrics;
      std::vector<SummaryDailyRow> daily;
      int64_t                      date_from = 0;
      int64_t                      date_to_excl = 0;
   };

   //--- daily covers [date_from - 86400, date_to_excl] so we can compute
   //--- floating-pnl change for the first day in range.
   Result Build(
      const ManagerRow& mgr,
      const CompiledFilters& filters,
      const std::vector<UserInfo>& users,
      const std::unordered_map<uint64_t, std::vector<DealRow>>& deals,
      const std::unordered_map<uint64_t, std::vector<DailyRow>>& daily,
      int64_t date_from, int64_t date_to_excl);
}
