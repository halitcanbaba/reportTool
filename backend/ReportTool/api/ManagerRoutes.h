#pragma once
#include "AppContext.h"
namespace httplib { class Server; }

namespace ManagerRoutes
{
   void Register(httplib::Server& srv, AppContext* ctx);
}
