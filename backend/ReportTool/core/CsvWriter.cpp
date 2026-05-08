//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       CsvWriter.cpp              |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "CsvWriter.h"
#include "TimeUtil.h"

CsvWriter::CsvWriter(const std::string& path, const std::vector<std::string>& header)
{
   fopen_s(&m_file, path.c_str(), "wb");
   if(!m_file) return;

   unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
   fwrite(bom, 1, 3, m_file);

   for(size_t i = 0; i < header.size(); ++i)
   {
      if(i) fputc(',', m_file);
      std::string esc = Escape(header[i]);
      fwrite(esc.data(), 1, esc.size(), m_file);
   }
   fputc('\n', m_file);
   fflush(m_file);
}

CsvWriter::~CsvWriter()
{
   if(m_in_row) fputc('\n', m_file);
   if(m_file) fclose(m_file);
}

void CsvWriter::EndRow()
{
   if(!m_file) return;
   if(m_in_row)
   {
      fputc('\n', m_file);
      m_in_row = false;
      m_rows++;
      if((m_rows & 0xFFF) == 0) fflush(m_file);
   }
}

void CsvWriter::Sep()
{
   if(!m_file) return;
   if(m_in_row) fputc(',', m_file);
   m_in_row = true;
}

CsvWriter& CsvWriter::Cell(const std::string& s)
{
   if(!m_file) return *this;
   Sep();
   std::string esc = Escape(s);
   fwrite(esc.data(), 1, esc.size(), m_file);
   return *this;
}

CsvWriter& CsvWriter::Cell(const char* s) { return Cell(std::string(s ? s : "")); }

CsvWriter& CsvWriter::Cell(double v, int digits)
{
   if(!m_file) return *this;
   Sep();
   if(!(v == v) || v == std::numeric_limits<double>::infinity()
                || v == -std::numeric_limits<double>::infinity())
      return *this;
   fprintf(m_file, "%.*f", digits, v);
   return *this;
}

CsvWriter& CsvWriter::Cell(int64_t  v) { if(!m_file) return *this; Sep(); fprintf(m_file, "%" PRId64, v); return *this; }
CsvWriter& CsvWriter::Cell(uint64_t v) { if(!m_file) return *this; Sep(); fprintf(m_file, "%" PRIu64, v); return *this; }
CsvWriter& CsvWriter::Cell(uint32_t v) { if(!m_file) return *this; Sep(); fprintf(m_file, "%u", v);       return *this; }
CsvWriter& CsvWriter::Cell(int      v) { if(!m_file) return *this; Sep(); fprintf(m_file, "%d", v);       return *this; }
CsvWriter& CsvWriter::Cell(bool     v) { return Cell(v ? std::string("true") : std::string("false")); }

CsvWriter& CsvWriter::CellDate(int64_t t)     { return Cell(TimeUtil::FormatDate(t)); }
CsvWriter& CsvWriter::CellDateTime(int64_t t) { return Cell(TimeUtil::FormatDateTime(t)); }

std::string CsvWriter::Escape(const std::string& s)
{
   bool needs_quote = (s.find(',')  != std::string::npos ||
                       s.find('"')  != std::string::npos ||
                       s.find('\n') != std::string::npos ||
                       s.find('\r') != std::string::npos);
   if(!needs_quote) return s;
   std::string out;
   out.reserve(s.size() + 4);
   out += '"';
   for(char c : s)
   {
      if(c == '"') out += "\"\"";
      else out += c;
   }
   out += '"';
   return out;
}
