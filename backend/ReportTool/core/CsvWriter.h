//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                  CsvWriter.h - BOM-UTF8 streaming CSV writer     |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

class CsvWriter
{
public:
   CsvWriter(const std::string& path, const std::vector<std::string>& header);
   ~CsvWriter();

   bool IsOpen() const { return m_file != nullptr; }
   void EndRow();

   CsvWriter& Cell(const std::string& s);
   CsvWriter& Cell(const char* s);
   CsvWriter& Cell(double v, int digits = 2);
   CsvWriter& Cell(int64_t v);
   CsvWriter& Cell(uint64_t v);
   CsvWriter& Cell(uint32_t v);
   CsvWriter& Cell(int v);
   CsvWriter& Cell(bool v);

   CsvWriter& CellDate(int64_t t);
   CsvWriter& CellDateTime(int64_t t);

   static std::string Escape(const std::string& s);
   uint64_t RowCount() const { return m_rows; }

private:
   FILE*    m_file   = nullptr;
   bool     m_in_row = false;
   uint64_t m_rows   = 0;

   void Sep();
};
