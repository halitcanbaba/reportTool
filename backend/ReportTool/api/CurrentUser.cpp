//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       CurrentUser.cpp           |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "CurrentUser.h"

namespace
{
   //--- thread_local works because cpp-httplib serves each request on a
   //--- worker thread; pre-routing sets, route handler reads, post-routing
   //--- clears — all within the same thread for that request.
   thread_local std::optional<::User>      g_user;
   thread_local std::optional<std::string> g_token;
}

void CurrentUser::Set(const ::User& u, const std::string& session_token)
{
   g_user  = u;
   g_token = session_token;
}

void CurrentUser::Clear()
{
   g_user.reset();
   g_token.reset();
}

std::optional<::User> CurrentUser::User()
{
   return g_user;
}

std::optional<std::string> CurrentUser::SessionToken()
{
   return g_token;
}
