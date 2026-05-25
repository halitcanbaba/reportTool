//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     PdfWriter.h - minimal in-memory PDF generator                |
//+------------------------------------------------------------------+
//--- Produces a one-or-multi-page PDF that opens in Acrobat / Chrome /
//--- Preview / Telegram's built-in viewer. Uses only the 14 standard
//--- PDF base fonts (Helvetica-Bold for title/headers, Courier for data)
//--- so no font embedding is needed; output stays small. Layout is a
//--- title + table dump, auto-paginated to A4 landscape — fine for the
//--- Telegram delivery use-case where the user wants a quick formal
//--- print of a scheduled report.
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>

namespace PdfWriter
{
   //--- Build the PDF as bytes. `headers` is row 0 (rendered bold).
   //--- Each cell is a string; numeric formatting is the caller's job.
   //--- Cells are truncated per-column to fit the page width.
   std::string Build(const std::string& title,
                     const std::vector<std::string>& headers,
                     const std::vector<std::vector<std::string>>& rows);
}
