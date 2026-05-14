//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     AccountFilterRoutes.h - CRUD for saved filter presets        |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace AccountFilterRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
