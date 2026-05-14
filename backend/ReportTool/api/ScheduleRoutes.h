//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     ScheduleRoutes.h - CRUD for scheduled ready-made reports     |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace ScheduleRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
