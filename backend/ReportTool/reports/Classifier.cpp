#include "../stdafx.h"
#include "Classifier.h"

int Classifier::Bucket(const DealRow& d, const CompiledFilters& f)
{
   if(d.action == IMTDeal::DEAL_CORRECTION) return 3; // always adjustment
   if(d.action != IMTDeal::DEAL_BALANCE) return 0;
   return f.Match(d.comment);
}
