//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       TopWinnerReport.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "TopWinnerReport.h"
#include "Classifier.h"

namespace
{
   //--- pick the latest daily row at-or-before target_t (< +86400 boundary).
   const DailyRow* PickLatestAtOrBefore(const std::vector<DailyRow>& v, int64_t target_t)
   {
      const DailyRow* hit = nullptr;
      for(const auto& r : v)
      {
         if(r.datetime <= target_t) hit = &r;
         else break; // assume sorted asc
      }
      return hit;
   }
}

TopWinnerReport::Result TopWinnerReport::Build(
   const ManagerRow& mgr,
   const CompiledFilters& filters,
   const std::vector<UserInfo>& users,
   const std::unordered_map<uint64_t, std::vector<DealRow>>& deals,
   const std::unordered_map<uint64_t, std::vector<DailyRow>>& boundary_open,
   const std::unordered_map<uint64_t, std::vector<DailyRow>>& boundary_close,
   int64_t date_from, int64_t date_to_excl,
   uint32_t top_n)
{
   Result out;
   out.date_from = date_from;
   out.date_to   = date_to_excl;
   out.total_logins = (uint32_t)users.size();

   //--- aggregate per login
   std::unordered_map<uint64_t, TopWinnerRow> agg;
   agg.reserve(users.size());
   for(const auto& u : users) { TopWinnerRow& r = agg[u.login]; r.login = u.login; }

   const int64_t open_target  = date_from - 86400;
   const int64_t close_target = date_to_excl - 86400;

   for(auto& kv : agg)
   {
      const uint64_t L = kv.first;
      TopWinnerRow& r = kv.second;

      //--- floating PL change + net equity from boundary daily snapshots
      double f_open = 0.0, f_close = 0.0, eq_close = 0.0;
      auto it_o = boundary_open.find(L);
      if(it_o != boundary_open.end())
      {
         const DailyRow* p = PickLatestAtOrBefore(it_o->second, open_target);
         if(p) f_open = p->profit;
      }
      auto it_c = boundary_close.find(L);
      if(it_c != boundary_close.end())
      {
         const DailyRow* p = PickLatestAtOrBefore(it_c->second, close_target);
         if(p) { f_close = p->profit; eq_close = p->profit_equity; }
      }
      r.floating_pl_change = f_close - f_open;
      r.net_equity         = eq_close;

      //--- per-deal walk
      auto it_d = deals.find(L);
      if(it_d != deals.end())
      {
         for(const auto& d : it_d->second)
         {
            if(d.time < date_from || d.time >= date_to_excl) continue;

            if(d.action == IMTDeal::DEAL_BUY || d.action == IMTDeal::DEAL_SELL)
            {
               r.closed_pl += d.profit;
               continue;
            }
            const int b = Classifier::Bucket(d, filters);
            switch(b)
            {
               case  1: r.deposit           += std::abs(d.profit); break;
               case -1: r.withdrawal        -= std::abs(d.profit); break;
               case  2: r.balance_writeoff  += d.profit;           break;
               case  3: r.trade_adjustments += d.profit;           break;
               default: break;
            }
         }
      }

      r.net_deposit = r.deposit + r.withdrawal;
      r.company_pl  = -(r.closed_pl + r.floating_pl_change + r.net_deposit
                       + r.balance_writeoff + r.trade_adjustments);
   }

   //--- sort by client total PnL desc and slice top_n
   out.rows.reserve(agg.size());
   for(auto& kv : agg) out.rows.push_back(std::move(kv.second));
   std::sort(out.rows.begin(), out.rows.end(),
             [](const TopWinnerRow& a, const TopWinnerRow& b) {
                return (a.closed_pl + a.floating_pl_change) > (b.closed_pl + b.floating_pl_change);
             });
   if(out.rows.size() > top_n) out.rows.resize(top_n);

   //--- header text
   char buf[256];
   snprintf(buf, sizeof(buf), "%s Top %u Winner Client", mgr.region.c_str(), top_n);
   out.header = buf;
   return out;
}
