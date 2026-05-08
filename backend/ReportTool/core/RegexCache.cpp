#include "../stdafx.h"
#include "RegexCache.h"

namespace
{
   bool CompileList(const std::vector<std::string>& patterns,
                    std::vector<std::regex>* out, std::string* err, const char* kind)
   {
      out->clear();
      out->reserve(patterns.size());
      for(const auto& p : patterns)
      {
         if(p.empty()) continue;
         try { out->emplace_back(p, std::regex::ECMAScript | std::regex::icase); }
         catch(const std::regex_error& e)
         {
            if(err) { *err = std::string("invalid ") + kind + " regex \"" + p + "\": " + e.what(); }
            return false;
         }
      }
      return true;
   }

   bool MatchAny(const std::string& s, const std::vector<std::regex>& regs)
   {
      for(const auto& r : regs)
         if(std::regex_search(s, r)) return true;
      return false;
   }
}

bool CompiledFilters::Compile(const RegexFilters& src, CompiledFilters* dst, std::string* err)
{
   if(!CompileList(src.deposit,    &dst->deposit,    err, "deposit"))    return false;
   if(!CompileList(src.withdrawal, &dst->withdrawal, err, "withdrawal")) return false;
   if(!CompileList(src.writeoff,   &dst->writeoff,   err, "writeoff"))   return false;
   if(!CompileList(src.adjustment, &dst->adjustment, err, "adjustment")) return false;
   return true;
}

int CompiledFilters::Match(const std::string& comment) const
{
   if(MatchAny(comment, deposit))    return  1;
   if(MatchAny(comment, withdrawal)) return -1;
   if(MatchAny(comment, writeoff))   return  2;
   if(MatchAny(comment, adjustment)) return  3;
   return 0;
}
