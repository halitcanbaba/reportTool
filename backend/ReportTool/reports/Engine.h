//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     Engine.h - generic template execution pipeline               |
//+------------------------------------------------------------------+
#pragma once
#include "../api/AppContext.h"
#include <string>

namespace Engine
{
   //--- Executes the given job's template against MT5 and writes outputs.
   //--- Throws on fatal errors (network, validation, missing template). The
   //--- JobRunner is responsible for marking the job Failed on exception.
   //--- Progress updates are emitted to JobRepo at key milestones.
   void Run(AppContext& ctx, int64_t job_id);
}
