//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     CurrentUser.h - per-thread current-request user accessor    |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include <optional>
#include <string>

namespace CurrentUser
{
   //--- Set by HttpServer pre-routing middleware once the session cookie
   //--- has been validated. Cleared by the post-routing handler.
   void Set(const User& u, const std::string& session_token);
   void Clear();

   //--- Read inside route handlers. nullopt outside an authenticated request.
   std::optional<User>        User();
   std::optional<std::string> SessionToken();
}
