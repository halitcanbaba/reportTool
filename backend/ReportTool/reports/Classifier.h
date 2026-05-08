//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|         Classifier.h - bucket a DealRow into deposit/wd/wo/adj    |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../core/RegexCache.h"

namespace Classifier
{
   //--- Bucket numbers
   //--- 1 = deposit, -1 = withdrawal, 2 = writeoff, 3 = adjustment, 0 = none.
   //--- Forces DEAL_CORRECTION into adjustment regardless of comment.
   //--- Forces non-balance/non-correction actions to 0.
   int Bucket(const DealRow& d, const CompiledFilters& f);
}
