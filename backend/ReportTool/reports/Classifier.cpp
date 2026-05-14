#include "../stdafx.h"
#include "Classifier.h"

int Classifier::Bucket(const DealRow& d, const CompiledFilters& f)
{
   if(d.action == IMTDeal::DEAL_CORRECTION) return 3;        // always adjustment
   if(d.action != IMTDeal::DEAL_BALANCE)    return 0;

   //--- First try comment-regex classification (deposit > withdrawal > writeoff > adjustment).
   const int regex_bucket = f.Match(d.comment);
   if(regex_bucket != 0) return regex_bucket;

   //--- Fallback: when the user hasn't configured a regex list for that
   //--- category, classify DEAL_BALANCE by profit sign. This makes the
   //--- engine produce sensible deposit/withdrawal totals out of the box
   //--- (no per-manager regex required). If the user *has* defined a list
   //--- for that category, we respect it strictly — no implicit catch-all.
   if(d.profit > 0 && f.deposit.empty())    return  1;        // deposit
   if(d.profit < 0 && f.withdrawal.empty()) return -1;        // withdrawal
   return 0;
}
