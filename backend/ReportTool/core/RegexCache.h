//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|              RegexCache.h - Compile regex lists once per request |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include <regex>
#include <vector>
#include <string>

struct CompiledFilters
{
   std::vector<std::regex> deposit;
   std::vector<std::regex> withdrawal;
   std::vector<std::regex> writeoff;
   std::vector<std::regex> adjustment;

   static bool Compile(const RegexFilters& src, CompiledFilters* dst, std::string* err);

   //--- Bucket precedence: deposit > withdrawal > writeoff > adjustment.
   //--- Returns: 1=deposit, -1=withdrawal, 2=writeoff, 3=adjustment, 0=none.
   int Match(const std::string& comment) const;
};
