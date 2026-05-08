#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace HealthRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
