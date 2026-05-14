//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     GenericWriter.h - column-list CSV + JSON preview emitter     |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../third_party/json.hpp"
#include <string>
#include <vector>
#include <variant>

namespace GenericWriter
{
   //--- Cell value: text identifier OR computed double. Null = missing.
   struct Cell
   {
      enum class Kind { Null, Number, Text };
      Kind        kind = Kind::Null;
      double      number = 0.0;
      std::string text;

      static Cell N() { return {}; }
      static Cell Num(double v) { Cell c; c.kind = Kind::Number; c.number = v; return c; }
      static Cell Txt(std::string s) { Cell c; c.kind = Kind::Text; c.text = std::move(s); return c; }
   };

   //--- Write CSV with UTF-8 BOM. Returns the filename written (empty on failure).
   std::string WriteCsv(const std::vector<ColumnSpec>& columns,
                        const std::vector<std::vector<Cell>>& rows,
                        const std::string& dir,
                        int64_t job_id,
                        const std::string& report_title);

   //--- JSON preview for /api/reports/jobs/:id GET.
   //--- preview_max: cap rows returned in JSON (full rows in CSV). 0 = all.
   nlohmann::json ToJson(const ReportTemplate& tpl,
                         const std::vector<std::vector<Cell>>& rows,
                         uint32_t total_logins,
                         int64_t date_from,
                         int64_t date_to,
                         size_t preview_max);
}
