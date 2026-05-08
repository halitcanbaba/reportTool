//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|              Records.h - POD types shared across modules         |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <limits>

//--- Mirrors of analyzer's records (UserInfo / DealRow / DailyRow) ---

struct UserInfo
{
   uint64_t    login    = 0;
   std::string group;
   std::string currency;
   std::string name;
   uint32_t    leverage = 0;
   int64_t     registration = 0;   // Unix seconds
};

struct DealRow
{
   uint64_t    ticket   = 0;
   uint64_t    login    = 0;
   uint64_t    position_id = 0;
   uint32_t    action   = 0;
   uint32_t    entry    = 0;
   uint32_t    reason   = 0;
   int64_t     time     = 0;
   double      profit   = 0.0;
   double      storage  = 0.0;
   double      commission = 0.0;
   double      fee      = 0.0;
   double      volume   = 0.0;
   double      price    = 0.0;
   std::string symbol;
   std::string comment;
};

struct DailyRow
{
   uint64_t login        = 0;
   int64_t  datetime     = 0;
   double   balance      = 0.0;
   double   profit_equity= 0.0;   // EOD equity
   double   margin       = 0.0;
   double   profit       = 0.0;   // floating
   double   daily_profit = 0.0;
   double   daily_balance= 0.0;
   double   daily_credit = 0.0;
   double   daily_storage= 0.0;
   std::string currency;
   uint32_t currency_digits = 2;
};

//--- ReportTool-specific types ---

struct RegexFilters
{
   std::vector<std::string> deposit;
   std::vector<std::string> withdrawal;
   std::vector<std::string> writeoff;
   std::vector<std::string> adjustment;
};

struct ManagerRow
{
   int64_t      id           = 0;
   std::string  name;
   std::string  brand;
   std::string  region;
   std::string  server;       // host:port
   uint64_t     manager_login = 0;
   std::string  password;     // plaintext after decrypt
   std::vector<std::string> group_masks;   // MT5 wildcard list
   std::string  group_regex;                // optional ECMAScript post-filter
   uint64_t     login_min     = 0;          // 0 = unset
   uint64_t     login_max     = 0;          // 0 = unset
   bool         active        = true;
   int64_t      created_at    = 0;
   int64_t      updated_at    = 0;
   RegexFilters regex_filters;
};

//--- Top Winner report row ------------------------------------------

struct TopWinnerRow
{
   uint64_t login              = 0;
   double   deposit            = 0.0;   // positive
   double   withdrawal         = 0.0;   // negative (kept)
   double   net_deposit        = 0.0;   // deposit + withdrawal
   double   closed_pl          = 0.0;
   double   floating_pl_change = 0.0;
   double   balance_writeoff   = 0.0;
   double   trade_adjustments  = 0.0;
   double   net_equity         = 0.0;
   double   company_pl         = 0.0;
};

//--- Summary report types -------------------------------------------

struct SummaryDailyRow
{
   int64_t     date                   = 0;   // UTC midnight
   std::string brand;
   double      deposit                = 0.0;
   double      withdrawal             = 0.0; // negative
   double      net_deposit            = 0.0;
   double      closed_pnl             = 0.0;
   double      floating_pnl_change    = 0.0;
   double      negative_equity_change = 0.0;
   double      todays_total_equity    = 0.0;
   uint32_t    new_accounts           = 0;
   double      balance_writeoff       = 0.0;  // not displayed but used in company_pnl
   double      trade_adjustments      = 0.0;
   double      company_pnl            = 0.0;
};

struct SummaryMetrics
{
   std::string brand;
   double      monthly_deposit          = 0.0;
   double      monthly_withdrawal       = 0.0;
   double      monthly_net_deposit      = 0.0;
   double      todays_total_equity      = 0.0;
   double      yesterdays_total_equity  = 0.0;
   double      equity_change_pct        = 0.0;
   uint32_t    daily_new_accounts       = 0;
   uint32_t    monthly_new_accounts     = 0;
   double      monthly_company_pnl      = 0.0;
};

//--- Job tracking ---------------------------------------------------

enum class JobStatus { Queued, Running, Completed, Failed };

inline const char* JobStatusName(JobStatus s)
{
   switch(s)
   {
      case JobStatus::Queued:    return "queued";
      case JobStatus::Running:   return "running";
      case JobStatus::Completed: return "completed";
      case JobStatus::Failed:    return "failed";
   }
   return "?";
}

enum class ReportKind { TopWinner, Summary };

inline const char* ReportKindName(ReportKind k)
{
   return k == ReportKind::TopWinner ? "top_winner" : "summary";
}

struct JobRow
{
   int64_t      id            = 0;
   int64_t      manager_id    = 0;
   ReportKind   kind          = ReportKind::TopWinner;
   std::string  params_json;
   JobStatus    status        = JobStatus::Queued;
   double       progress      = 0.0;
   std::string  error_message;
   std::string  output_dir;
   std::string  csv_filename;
   std::string  xlsx_filename;
   std::string  summary_json;
   int64_t      created_at    = 0;
   int64_t      started_at    = 0;
   int64_t      completed_at  = 0;
};
