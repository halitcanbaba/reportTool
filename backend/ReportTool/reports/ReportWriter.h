//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     ReportWriter.h - emit CSV (and optionally XLSX)              |
//+------------------------------------------------------------------+
#pragma once
#include "TopWinnerReport.h"
#include "SummaryReport.h"
#include <string>

namespace ReportWriter
{
   //--- Write CSV; returns the absolute filename written (empty on failure).
   std::string WriteTopWinnerCsv(const TopWinnerReport::Result& r,
                                 const std::string& dir,
                                 int64_t job_id);

   std::string WriteSummaryCsv  (const SummaryReport::Result& r,
                                 const std::string& dir,
                                 int64_t job_id);

   //--- JSON preview (rendered in API GET /jobs/:id) — compact, suitable for UI.
   std::string TopWinnerToJson(const TopWinnerReport::Result& r);
   std::string SummaryToJson  (const SummaryReport::Result& r);
}
