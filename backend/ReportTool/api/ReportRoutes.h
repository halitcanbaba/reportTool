#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace ReportRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
