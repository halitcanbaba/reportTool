//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|              ConnectionPool.h - Cache IMTManagerAPI per manager  |
//+------------------------------------------------------------------+
#pragma once
#include "Connection.h"
#include "../core/Records.h"
#include "../core/Logger.h"
#include <memory>
#include <map>
#include <shared_mutex>

class ConnectionPool
{
public:
   explicit ConnectionPool(Logger& log) : m_log(log) {}
   ~ConnectionPool() { Clear(); }

   //--- Returns an existing connection if cached, else connects fresh and caches.
   //--- Caller must hold the returned shared_ptr for the duration of API calls;
   //--- serialize per-connection calls with conn->CallMutex().
   std::shared_ptr<Connection> GetOrConnect(const ManagerRow& mgr);

   //--- Drop cached connection (e.g. after auth error)
   void Drop(int64_t manager_id);
   void Clear();

private:
   struct Entry
   {
      std::shared_ptr<Connection> conn;
      uint64_t                    login = 0;
      std::string                 server;
   };

   Logger&                            m_log;
   mutable std::shared_mutex          m_mu;
   std::map<int64_t, Entry>           m_pool;
};
