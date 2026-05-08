//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       SummaryReport.cpp          |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "SummaryReport.h"
#include "Classifier.h"
#include "../core/TimeUtil.h"

namespace
{
   //--- map of (login, day) -> DailyRow snapshot
   struct DailyIndex
   {
      std::unordered_map<uint64_t, std::unordered_map<int64_t, DailyRow>> per_login;
      const DailyRow* Find(uint64_t L, int64_t day) const
      {
         auto it = per_login.find(L);
         if(it == per_login.end()) return nullptr;
         auto jt = it->second.find(day);
         if(jt == it->second.end()) return nullptr;
         return &jt->second;
      }
   };

   DailyIndex BuildIndex(const std::unordered_map<uint64_t, std::vector<DailyRow>>& src)
   {
      DailyIndex idx;
      for(const auto& kv : src)
      {
         auto& m = idx.per_login[kv.first];
         for(const auto& r : kv.second)
            m[TimeUtil::UtcMidnight(r.datetime)] = r;
      }
      return idx;
   }
}

SummaryReport::Result SummaryReport::Build(
   const ManagerRow& mgr,
   const CompiledFilters& filters,
   const std::vector<UserInfo>& users,
   const std::unordered_map<uint64_t, std::vector<DealRow>>& deals,
   const std::unordered_map<uint64_t, std::vector<DailyRow>>& daily,
   int64_t date_from, int64_t date_to_excl)
{
   Result out;
   out.date_from = date_from;
   out.date_to_excl = date_to_excl;
   out.metrics.brand = mgr.brand;

   //--- 1) initialise day buckets
   std::map<int64_t, SummaryDailyRow> day_idx;
   for(int64_t d = date_from; d < date_to_excl; d += 86400)
   {
      SummaryDailyRow row; row.date = d; row.brand = mgr.brand;
      day_idx[d] = row;
   }

   //--- 2) walk deals, bucket per day
   for(const auto& kv : deals)
   {
      for(const auto& d : kv.second)
      {
         if(d.time < date_from || d.time >= date_to_excl) continue;
         const int64_t day = TimeUtil::UtcMidnight(d.time);
         auto it = day_idx.find(day);
         if(it == day_idx.end()) continue;
         SummaryDailyRow& row = it->second;

         if(d.action == IMTDeal::DEAL_BUY || d.action == IMTDeal::DEAL_SELL)
         {
            row.closed_pnl += d.profit;
            continue;
         }
         const int b = Classifier::Bucket(d, filters);
         switch(b)
         {
            case  1: row.deposit           += std::abs(d.profit); break;
            case -1: row.withdrawal        -= std::abs(d.profit); break;
            case  2: row.balance_writeoff  += d.profit;           break;
            case  3: row.trade_adjustments += d.profit;           break;
            default: break;
         }
      }
   }

   //--- 3) daily-driven equity / floating columns
   DailyIndex didx = BuildIndex(daily);
   //--- For each day, sum across logins; compare with previous day for floating change.
   //--- For "Negative Equity Change": logins crossing from >=0 yest to <0 today.
   for(auto& kv : day_idx)
   {
      const int64_t day = kv.first;
      const int64_t yesterday = day - 86400;
      double today_eq = 0.0, today_fl = 0.0, yest_fl = 0.0;
      double neg_change = 0.0;
      uint32_t today_present = 0;
      for(const auto& u : users)
      {
         const DailyRow* pt = didx.Find(u.login, day);
         const DailyRow* py = didx.Find(u.login, yesterday);
         if(pt) { today_eq += pt->profit_equity; today_fl += pt->profit; today_present++; }
         if(py) { yest_fl  += py->profit; }

         if(pt && py && pt->profit_equity < 0 && py->profit_equity >= 0)
            neg_change += pt->profit_equity; // amount that went underwater
      }
      kv.second.todays_total_equity    = today_eq;
      kv.second.floating_pnl_change    = today_fl - yest_fl;
      kv.second.negative_equity_change = neg_change;
   }

   //--- 4) new accounts per day
   for(const auto& u : users)
   {
      if(u.registration <= 0) continue;
      const int64_t day = TimeUtil::UtcMidnight(u.registration);
      auto it = day_idx.find(day);
      if(it != day_idx.end()) it->second.new_accounts++;
   }

   //--- 5) Net Deposit + Company PnL per day
   for(auto& kv : day_idx)
   {
      SummaryDailyRow& r = kv.second;
      r.net_deposit = r.deposit + r.withdrawal;
      r.company_pnl = -(r.closed_pnl + r.floating_pnl_change + r.net_deposit
                       + r.balance_writeoff + r.trade_adjustments);
   }

   //--- 6) flatten into Result.daily
   out.daily.reserve(day_idx.size());
   for(auto& kv : day_idx) out.daily.push_back(std::move(kv.second));

   //--- 7) top metric block
   for(const auto& r : out.daily)
   {
      out.metrics.monthly_deposit       += r.deposit;
      out.metrics.monthly_withdrawal    += r.withdrawal;
      out.metrics.monthly_net_deposit   += r.net_deposit;
      out.metrics.monthly_new_accounts  += r.new_accounts;
      out.metrics.monthly_company_pnl   += r.company_pnl;
   }

   if(!out.daily.empty())
   {
      const SummaryDailyRow& last = out.daily.back();
      out.metrics.todays_total_equity = last.todays_total_equity;
      out.metrics.daily_new_accounts  = last.new_accounts;

      //--- Yesterday's equity = sum across logins on (last.date - 86400)
      const int64_t yesterday = last.date - 86400;
      double yest_eq = 0.0;
      for(const auto& u : users)
      {
         const DailyRow* py = didx.Find(u.login, yesterday);
         if(py) yest_eq += py->profit_equity;
      }
      out.metrics.yesterdays_total_equity = yest_eq;
      const double yabs = std::max(std::abs(yest_eq), 1.0);
      out.metrics.equity_change_pct =
         (out.metrics.todays_total_equity - yest_eq) / yabs * 100.0;
   }

   //--- header
   out.header = mgr.brand + " Monthly Figures";
   return out;
}
