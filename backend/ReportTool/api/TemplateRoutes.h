//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     TemplateRoutes.h - CRUD + field catalog + validation         |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace TemplateRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
