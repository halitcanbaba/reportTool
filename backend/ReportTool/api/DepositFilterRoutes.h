//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     DepositFilterRoutes.h — CRUD + preview for deposit filters   |
//|     (multi-bucket cash-flow classification presets)              |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace DepositFilterRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
