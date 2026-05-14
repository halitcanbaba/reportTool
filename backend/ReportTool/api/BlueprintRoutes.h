//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     BlueprintRoutes.h - CRUD for saved formula building blocks   |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace BlueprintRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
