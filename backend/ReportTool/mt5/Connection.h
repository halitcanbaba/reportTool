//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                          Connection.h - Manager API connection   |
//+------------------------------------------------------------------+
#pragma once
#include "../stdafx.h"
#include "../core/Logger.h"
#include <string>
#include <mutex>

class Connection
{
public:
   Connection() = default;
   ~Connection() { Disconnect(); }

   static bool  InitFactory(const std::string& dll_dir, Logger& log);
   static void  ShutdownFactory();

   bool  Connect(const std::string& server, uint64_t login,
                 const std::string& password, Logger& log,
                 uint64_t pump_mode = 0, uint32_t timeout_ms = 30000);

   bool  ConnectAdmin(const std::string& server, uint64_t login,
                      const std::string& password, Logger& log,
                      uint32_t timeout_ms = 30000);

   void  Disconnect();

   IMTManagerAPI* Api()   const { return m_manager; }
   IMTAdminAPI*   Admin() const { return m_admin; }
   bool IsConnected() const { return m_manager != nullptr || m_admin != nullptr; }

   //--- per-connection serialization for IMTManagerAPI calls (SDK thread-safety
   //--- is undocumented; we serialize to be safe).
   std::mutex& CallMutex() { return m_call_mu; }

private:
   IMTManagerAPI*               m_manager = nullptr;
   IMTAdminAPI*                 m_admin   = nullptr;
   std::mutex                   m_call_mu;

   static CMTManagerAPIFactory  s_factory;
   static bool                  s_factory_init;

   static std::wstring ToWide(const std::string& s);
};
