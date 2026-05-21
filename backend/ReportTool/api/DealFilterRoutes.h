//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     DealFilterRoutes.h - CRUD + preview for saved deal filters   |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace DealFilterRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
