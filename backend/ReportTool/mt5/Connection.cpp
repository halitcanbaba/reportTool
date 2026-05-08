//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Connection.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Connection.h"

CMTManagerAPIFactory Connection::s_factory;
bool                 Connection::s_factory_init = false;

bool Connection::InitFactory(const std::string& dll_dir, Logger& log)
{
   if(s_factory_init) return true;

   std::wstring wpath = ToWide(dll_dir.empty() ? "." : dll_dir);
   MTAPIRES res = s_factory.Initialize(wpath.c_str());
   if(res != MT_RET_OK)
   {
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
      log.Error("CreateManager failed: %u", res);
      return false;
   }

   std::wstring wserver = ToWide(server);
   std::wstring wpassword = ToWide(password);

   res = m_manager->Connect(wserver.c_str(), login, wpassword.c_str(),
                            L"", pump_mode, timeout_ms);
   if(res != MT_RET_OK)
   {
      log.Error("Connect to %s (login=%llu) failed: %u", server.c_str(), login, res);
      m_manager->Release(); m_manager = nullptr;
      return false;
   }

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
