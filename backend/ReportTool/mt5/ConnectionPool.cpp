#include "../stdafx.h"
#include "ConnectionPool.h"

std::shared_ptr<Connection> ConnectionPool::GetOrConnect(const ManagerRow& mgr)
{
   {
      std::shared_lock<std::shared_mutex> lk(m_mu);
      auto it = m_pool.find(mgr.id);
      if(it != m_pool.end()
         && it->second.login  == mgr.manager_login
         && it->second.server == mgr.server
         && it->second.conn && it->second.conn->IsConnected())
      {
         return it->second.conn;
      }
   }

   //--- need to (re)create
   std::unique_lock<std::shared_mutex> lk(m_mu);
   auto it = m_pool.find(mgr.id);
   if(it != m_pool.end()
      && it->second.login  == mgr.manager_login
      && it->second.server == mgr.server
      && it->second.conn && it->second.conn->IsConnected())
   {
      return it->second.conn;
   }

   auto conn = std::make_shared<Connection>();
   if(!conn->Connect(mgr.server, mgr.manager_login, mgr.password, m_log))
      return nullptr;

   Entry e;
   e.conn = conn; e.login = mgr.manager_login; e.server = mgr.server;
   m_pool[mgr.id] = e;
   return conn;
}

void ConnectionPool::Drop(int64_t manager_id)
{
   std::unique_lock<std::shared_mutex> lk(m_mu);
   m_pool.erase(manager_id);
}

void ConnectionPool::Clear()
{
   std::unique_lock<std::shared_mutex> lk(m_mu);
   m_pool.clear();
}
