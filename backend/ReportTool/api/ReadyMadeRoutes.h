//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     ReadyMadeRoutes.h - CRUD + Run for saved report bundles      |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace ReadyMadeRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
