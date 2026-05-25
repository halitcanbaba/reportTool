//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     XlsxWriter.h - minimal in-memory XLSX (OOXML) generator      |
//+------------------------------------------------------------------+
//--- Produces a valid xlsx (ZIP of XML) that Excel / Google Sheets / Numbers
//--- opens. No external libraries: a small store-mode ZIP writer + just
//--- enough OOXML markup. Strings are inline (no sharedStrings.xml) and
//--- the file uses no custom styles, so numeric cells render in Excel's
//--- General format — fine for the Telegram-delivered "machine-readable
//--- export" use-case. Output is uncompressed; trade-off accepted because
//--- this is intended for small-to-medium report payloads.
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>

namespace XlsxWriter
{
   //--- One cell. `is_number` flips OOXML cell type between numeric and
   //--- inline-string. For "money"/"int"/"pct"/"number" columns the caller
   //--- pre-parses and sets is_number=true; everything else (text, dates)
   //--- goes as a string so the value reads back exactly as written.
   struct Cell {
      std::string text;
      bool        is_number = false;
   };

   //--- Build the .xlsx file as bytes. `sheet_name` should be ≤31 chars
   //--- (Excel limit) and free of : \ / ? * [ ]. `headers` becomes row 1.
   std::string Build(const std::string& sheet_name,
                     const std::vector<std::string>& headers,
                     const std::vector<std::vector<Cell>>& rows);
}
