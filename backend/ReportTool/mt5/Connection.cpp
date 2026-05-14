//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Connection.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Connection.h"

CMTManagerAPIFactory Connection::s_factory;
bool                 Connection::s_factory_init = false;
MTAPIRES             Connection::s_last_err     = MT_RET_OK;

std::string Connection::LastErrorString()
{
   //--- Map common MTAPIRES codes to friendly hints. Full list lives in
   //--- the MT5 SDK; we surface the numeric code so unknown ones are still
   //--- diagnosable.
   const MTAPIRES r = s_last_err;
   char buf[128];
   const char* hint = "";
   switch(r)
   {
      case MT_RET_OK:                 hint = "ok";                      break;
      case MT_RET_OK_NONE:            hint = "ok-none";                 break;
      case MT_RET_ERROR:              hint = "generic error";           break;
      case MT_RET_ERR_PARAMS:         hint = "invalid parameters";      break;
      case MT_RET_ERR_NETWORK:        hint = "network error";           break;
      case MT_RET_ERR_NOTFOUND:       hint = "not found";               break;
      case MT_RET_ERR_TIMEOUT:        hint = "timeout";                 break;
      case MT_RET_ERR_NOTIMPLEMENT:   hint = "not implemented";         break;
      case MT_RET_ERR_NOTSUPPORTED:   hint = "not supported";           break;
      case MT_RET_ERR_CANCEL:         hint = "canceled";                break;
      case MT_RET_ERR_PERMISSIONS:    hint = "access denied / auth";    break;
      case MT_RET_ERR_CONNECTION:     hint = "connection error";        break;
      default:                        hint = "see MT5 SDK MTAPIRES";    break;
   }
   snprintf(buf, sizeof(buf), "MTAPI code=%u (%s)", (unsigned)r, hint);
   return buf;
}

bool Connection::InitFactory(const std::string& dll_dir, Logger& log)
{
   if(s_factory_init) return true;

   std::wstring wpath = ToWide(dll_dir.empty() ? "." : dll_dir);
   MTAPIRES res = s_factory.Initialize(wpath.c_str());
   if(res != MT_RET_OK)
   {
      s_last_err = res;
      log.Error("CMTManagerAPIFactory::Initialize failed: %u (path: %s)", res, dll_dir.c_str());
      return false;
   }

   uint32_t version = 0;
   if(s_factory.Version(version) == MT_RET_OK)
      log.Info("Manager API version %u loaded", version);

   if(version < MTManagerAPIVersion)
   {
      log.Error("API version mismatch: loaded %u, required %u", version, MTManagerAPIVersion);
      s_factory.Shutdown();
      return false;
   }

   s_factory_init = true;
   return true;
}

void Connection::ShutdownFactory()
{
   if(s_factory_init) { s_factory.Shutdown(); s_factory_init = false; }
}

bool Connection::Connect(const std::string& server, uint64_t login,
                         const std::string& password, Logger& log,
                         uint64_t pump_mode, uint32_t timeout_ms)
{
   MTAPIRES res = s_factory.CreateManager(MTManagerAPIVersion, &m_manager);
   if(res != MT_RET_OK || !m_manager)
   {
      s_last_err = res;
      log.Error("CreateManager failed: %u", res);
      return false;
   }

   std::wstring wserver = ToWide(server);
   std::wstring wpassword = ToWide(password);

   res = m_manager->Connect(wserver.c_str(), login, wpassword.c_str(),
                            L"", pump_mode, timeout_ms);
   if(res != MT_RET_OK)
   {
      s_last_err = res;
      log.Error("Connect to %s (login=%llu) failed: %u", server.c_str(), login, res);
      m_manager->Release(); m_manager = nullptr;
      return false;
   }

   s_last_err = MT_RET_OK;
   log.Info("Connected to %s as login %llu", server.c_str(), login);
   return true;
}

bool Connection::ConnectAdmin(const std::string& server, uint64_t login,
                              const std::string& password, Logger& log,
                              uint32_t timeout_ms)
{
   MTAPIRES res = s_factory.CreateAdmin(MTManagerAPIVersion, &m_admin);
   if(res != MT_RET_OK || !m_admin)
   {
      log.Error("CreateAdmin failed: %u", res);
      return false;
   }

   std::wstring wserver = ToWide(server);
   std::wstring wpassword = ToWide(password);

   res = m_admin->Connect(wserver.c_str(), login, wpassword.c_str(), L"", 0, timeout_ms);
   if(res != MT_RET_OK)
   {
      log.Error("Admin Connect to %s (login=%llu) failed: %u", server.c_str(), login, res);
      m_admin->Release(); m_admin = nullptr;
      return false;
   }

   log.Info("Admin connected to %s as login %llu", server.c_str(), login);
   return true;
}

void Connection::Disconnect()
{
   if(m_manager) { m_manager->Disconnect(); m_manager->Release(); m_manager = nullptr; }
   if(m_admin)   { m_admin->Disconnect();   m_admin->Release();   m_admin   = nullptr; }
}

std::wstring Connection::ToWide(const std::string& s)
{
   if(s.empty()) return L"";
   int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
   std::wstring result(len, L'\0');
   MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &result[0], len);
   if(!result.empty() && result.back() == L'\0') result.pop_back();
   return result;
}
