//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     EvalContext.h - per-login + shared eval state for one run    |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../core/RegexCache.h"
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

struct EvalContext
{
   //--- Per-login data sources (any may be nullptr if not fetched).
   const UserInfo*                              user           = nullptr;
   const AccountInfo*                           account        = nullptr;
   const std::vector<DailyRow>*                 daily          = nullptr;
   const std::vector<DealRow>*                  deals          = nullptr;
   const std::vector<PositionRow>*              positions      = nullptr;
   const std::vector<OpenOrderRow>*             open_orders    = nullptr;
   const std::vector<HistoryOrderRow>*          history_orders = nullptr;

   //--- Shared across all logins for one job.
   const std::map<std::string, int64_t>* date_params = nullptr;  // name → Unix seconds (00:00 UTC)
   const CompiledFilters*                filters     = nullptr;  // deal bucket regex
};
