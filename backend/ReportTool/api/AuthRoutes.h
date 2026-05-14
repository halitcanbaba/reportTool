//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     AuthRoutes.h - setup / login / logout / me / password       |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace AuthRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
