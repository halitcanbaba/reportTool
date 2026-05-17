//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     FolderRoutes.h - CRUD for user-defined organisational folders |
//+------------------------------------------------------------------+
#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace FolderRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
