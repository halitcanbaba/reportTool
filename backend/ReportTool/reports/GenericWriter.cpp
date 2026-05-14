//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       GenericWriter.cpp          |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "GenericWriter.h"
#include "../core/CsvWriter.h"
#include "../core/TimeUtil.h"

using nlohmann::json;

namespace
{
   int DigitsForFormat(ColumnSpec::Format f)
   {
      switch(f)
      {
         case ColumnSpec::Format::Money: return 2;
         case ColumnSpec::Format::Pct:   return 2;
         case ColumnSpec::Format::Int:   return 0;
         default: return 2;
      }
   }

   void WriteCell(CsvWriter& w, const ColumnSpec& c, const GenericWriter::Cell& cell)
   {
      if(cell.kind == GenericWriter::Cell::Kind::Null)
      {
         w.Cell(std::string());
         return;
      }
      if(cell.kind == GenericWriter::Cell::Kind::Text)
      {
         w.Cell(cell.text);
         return;
      }
      // Number
      switch(c.format)
      {
         case ColumnSpec::Format::Date:
            w.CellDate((int64_t)cell.number);
            break;
         case ColumnSpec::Format::Int:
            w.Cell((int64_t)cell.number);
            break;
         default:
            w.Cell(cell.number, DigitsForFormat(c.format));
            break;
      }
   }
}

std::string GenericWriter::WriteCsv(const std::vector<ColumnSpec>& columns,
                                    const std::vector<std::vector<Cell>>& rows,
                                    const std::string& dir,
                                    int64_t job_id,
                                    const std::string& report_title)
{
   //--- Filename: <slug>_<stamp>_job<id>.csv
   std::string slug = report_title.empty() ? "report" : report_title;
   for(auto& c : slug) if(!isalnum((unsigned char)c)) c = '_';
   char buf[256];
   snprintf(buf, sizeof(buf), "%s_%s_job%lld.csv",
            slug.c_str(), TimeUtil::RunStamp().c_str(), (long long)job_id);
   const std::string path = dir + "/" + buf;

   std::vector<std::string> header;
   header.reserve(columns.size());
   for(const auto& c : columns) header.push_back(c.label);

   CsvWriter w(path, header);
   if(!w.IsOpen()) return "";

   for(const auto& row : rows)
   {
      for(size_t i = 0; i < columns.size(); ++i)
      {
         if(i < row.size()) WriteCell(w, columns[i], row[i]);
         else               w.Cell(std::string());
      }
      w.EndRow();
   }
   return buf;
}

json GenericWriter::ToJson(const ReportTemplate& tpl,
                           const std::vector<std::vector<Cell>>& rows,
                           uint32_t total_logins,
                           int64_t date_from,
                           int64_t date_to,
                           size_t preview_max)
{
   json columns = json::array();
   for(const auto& c : tpl.columns)
   {
      const char* kind_s = c.kind == ColumnSpec::Kind::Identifier ? "identifier" : "formula";
      const char* fmt_s  = "money";
      switch(c.format)
      {
         case ColumnSpec::Format::Money: fmt_s = "money"; break;
         case ColumnSpec::Format::Pct:   fmt_s = "pct";   break;
         case ColumnSpec::Format::Int:   fmt_s = "int";   break;
         case ColumnSpec::Format::Text:  fmt_s = "text";  break;
         case ColumnSpec::Format::Date:  fmt_s = "date";  break;
      }
      columns.push_back({ {"key", c.key}, {"label", c.label}, {"kind", kind_s}, {"format", fmt_s} });
   }

   json js_rows = json::array();
   const size_t limit = preview_max ? std::min(preview_max, rows.size()) : rows.size();
   for(size_t i = 0; i < limit; ++i)
   {
      const auto& r = rows[i];
      json row = json::array();
      for(size_t k = 0; k < tpl.columns.size(); ++k)
      {
         if(k >= r.size())                                 { row.push_back(nullptr); continue; }
         const auto& cell = r[k];
         if(cell.kind == Cell::Kind::Null)                  { row.push_back(nullptr); continue; }
         if(cell.kind == Cell::Kind::Text)                  { row.push_back(cell.text); continue; }
         row.push_back(cell.number);
      }
      js_rows.push_back(std::move(row));
   }

   return json{
      {"template_id",   tpl.id},
      {"template_name", tpl.name},
      {"columns",       columns},
      {"rows",          js_rows},
      {"total_logins",  total_logins},
      {"date_from",     date_from},
      {"date_to",       date_to},
   };
}
