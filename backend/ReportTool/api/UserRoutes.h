//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     UserRoutes.h - admin-only user management (CRUD)            |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace UserRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
