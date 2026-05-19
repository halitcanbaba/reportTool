#include "../stdafx.h"
#include "HealthRoutes.h"
#include "../third_party/httplib.h"

using nlohmann::json;

namespace { int64_t g_started_at = 0; }

void HealthRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   g_started_at = (int64_t)time(nullptr);
   srv.Get("/health", [ctx](const httplib::Request&, httplib::Response& res){
      json j = {
         { "status",   "ok" },
         { "version",  "2.0.5" },
         { "uptime_s", (int64_t)time(nullptr) - g_started_at },
      };
      res.set_content(j.dump(), "application/json");
   });
}
