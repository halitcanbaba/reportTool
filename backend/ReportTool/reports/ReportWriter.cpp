//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       ReportWriter.cpp           |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "ReportWriter.h"
#include "../core/CsvWriter.h"
#include "../core/TimeUtil.h"

using nlohmann::json;

std::string ReportWriter::WriteTopWinnerCsv(const TopWinnerReport::Result& r,
                                             const std::string& dir,
                                             int64_t job_id)
{
   char fname[64]; snprintf(fname, sizeof(fname), "topwinner_%lld.csv", (long long)job_id);
   std::string path = dir + "/" + fname;

   std::vector<std::string> header = {
      "Login","Deposit","Withdrawal","Net Deposit","Closed PL",
      "Floating PL Change","Balance Writeoff","Trade Adjustments",
      "Net Equity","Company PL"
   };
   CsvWriter w(path, header);
   if(!w.IsOpen()) return "";

   for(const auto& row : r.rows)
   {
      w.Cell(row.login)
       .Cell(row.deposit)
       .Cell(row.withdrawal)
       .Cell(row.net_deposit)
       .Cell(row.closed_pl)
       .Cell(row.floating_pl_change)
       .Cell(row.balance_writeoff)
       .Cell(row.trade_adjustments)
       .Cell(row.net_equity)
       .Cell(row.company_pl);
      w.EndRow();
   }
   return fname;
}

std::string ReportWriter::WriteSummaryCsv(const SummaryReport::Result& r,
                                           const std::string& dir,
                                           int64_t job_id)
{
   char fname[64]; snprintf(fname, sizeof(fname), "summary_%lld.csv", (long long)job_id);
   std::string path = dir + "/" + fname;

   std::vector<std::string> header = {
      "Date","Brand","Deposit","Withdrawal","Net Deposit","Closed PnL",
      "Floating PnL Change","Negative Equity Change","Today's Total Equity",
      "Number of New Accounts","Company PnL"
   };
   CsvWriter w(path, header);
   if(!w.IsOpen()) return "";

   for(const auto& row : r.daily)
   {
      w.CellDate(row.date)
       .Cell(row.brand)
       .Cell(row.deposit)
       .Cell(row.withdrawal)
       .Cell(row.net_deposit)
       .Cell(row.closed_pnl)
       .Cell(row.floating_pnl_change)
       .Cell(row.negative_equity_change)
       .Cell(row.todays_total_equity)
       .Cell(row.new_accounts)
       .Cell(row.company_pnl);
      w.EndRow();
   }
   return fname;
}

std::string ReportWriter::TopWinnerToJson(const TopWinnerReport::Result& r)
{
   json j;
   j["header"]    = r.header;
   j["date_from"] = r.date_from;
   j["date_to"]   = r.date_to;
   j["total_logins"] = r.total_logins;
   j["rows"]      = json::array();
   for(const auto& row : r.rows)
   {
      j["rows"].push_back({
         {"login",              row.login},
         {"deposit",            row.deposit},
         {"withdrawal",         row.withdrawal},
         {"net_deposit",        row.net_deposit},
         {"closed_pl",          row.closed_pl},
         {"floating_pl_change", row.floating_pl_change},
         {"balance_writeoff",   row.balance_writeoff},
         {"trade_adjustments",  row.trade_adjustments},
         {"net_equity",         row.net_equity},
         {"company_pl",         row.company_pl},
      });
   }
   return j.dump();
}

std::string ReportWriter::SummaryToJson(const SummaryReport::Result& r)
{
   json j;
   j["header"]   = r.header;
   j["metrics"]  = {
      {"brand",                   r.metrics.brand},
      {"monthly_deposit",         r.metrics.monthly_deposit},
      {"monthly_withdrawal",      r.metrics.monthly_withdrawal},
      {"monthly_net_deposit",     r.metrics.monthly_net_deposit},
      {"todays_total_equity",     r.metrics.todays_total_equity},
      {"yesterdays_total_equity", r.metrics.yesterdays_total_equity},
      {"equity_change_pct",       r.metrics.equity_change_pct},
      {"daily_new_accounts",      r.metrics.daily_new_accounts},
      {"monthly_new_accounts",    r.metrics.monthly_new_accounts},
      {"monthly_company_pnl",     r.metrics.monthly_company_pnl},
   };
   j["daily"] = json::array();
   for(const auto& row : r.daily)
   {
      j["daily"].push_back({
         {"date",                   row.date},
         {"brand",                  row.brand},
         {"deposit",                row.deposit},
         {"withdrawal",             row.withdrawal},
         {"net_deposit",            row.net_deposit},
         {"closed_pnl",             row.closed_pnl},
         {"floating_pnl_change",    row.floating_pnl_change},
         {"negative_equity_change", row.negative_equity_change},
         {"todays_total_equity",    row.todays_total_equity},
         {"new_accounts",           row.new_accounts},
         {"company_pnl",            row.company_pnl},
      });
   }
   return j.dump();
}
