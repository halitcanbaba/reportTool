//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     TopWinnerReport.h - per-login aggregation + top-N sort      |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../core/RegexCache.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace TopWinnerReport
{
   struct Result
   {
      std::string                header;          // "{region} Top {N} Winner Client"
      std::vector<TopWinnerRow>  rows;
      int64_t                    date_from = 0;
      int64_t                    date_to   = 0;
      uint32_t                   total_logins = 0;
   };

   Result Build(
      const ManagerRow& mgr,
      const CompiledFilters& filters,
      const std::vector<UserInfo>& users,
      const std::unordered_map<uint64_t, std::vector<DealRow>>& deals,
      const std::unordered_map<uint64_t, std::vector<DailyRow>>& boundary_open,   // at (from-1d)
      const std::unordered_map<uint64_t, std::vector<DailyRow>>& boundary_close,  // at (to-1d)
      int64_t date_from, int64_t date_to_excl,
      uint32_t top_n);
}
