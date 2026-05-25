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

   //--- All UserInfo / AccountInfo in the current pivot bucket. Populated by
   //--- multi-user pivots (group, country, city, comment, …) so Cat B / Cat C
   //--- accessors can aggregate (sum) across the bucket instead of returning
   //--- only the first user's value. For single-user buckets (login, ticket,
   //--- login+symbol, …) these contain exactly one entry — sum reduces to the
   //--- single value, preserving legacy behaviour.
   const std::vector<const UserInfo*>*    bucket_users    = nullptr;
   const std::vector<const AccountInfo*>* bucket_accounts = nullptr;

   //--- Shared across all logins for one job.
   const std::map<std::string, int64_t>* date_params = nullptr;  // name → Unix seconds (00:00 UTC)
   const CompiledFilters*                filters     = nullptr;  // deal bucket regex

   //--- Active DepositFilter for the current run (from ready-made's
   //--- deposit_filter_id or per-run override). The eight hardcoded
   //--- sum_cash_deposit / count_promotion / etc. fields each capture a
   //--- pointer-to-member into this struct and read their predicate from
   //--- it. Null when the run has no deposit_filter bound — those fields
   //--- then return 0.
   const DepositFilter*                  deposit_filter = nullptr;

   //--- Per-row cache of previously evaluated columns (key → numeric value).
   //--- Populated by Engine left-to-right; ExprNode::ColRef looks up here.
   //--- Forward / unknown refs read as 0.0.
   const std::unordered_map<std::string, double>* column_values = nullptr;

   //--- Pivot key for the current row. Engine fills these per RowContext;
   //--- identifier fields like `symbol` / `ticket` read from here when the
   //--- bucket has no underlying User.
   std::string pivot_key_text;
   double      pivot_key_num = 0.0;
};
