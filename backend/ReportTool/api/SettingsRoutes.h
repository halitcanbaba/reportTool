//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     SettingsRoutes.h - global app settings (telegram bot, …)     |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace SettingsRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
