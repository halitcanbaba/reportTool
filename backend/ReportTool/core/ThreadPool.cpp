#include "../stdafx.h"
#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t n)
{
   if(n == 0) n = 1;
   for(size_t i = 0; i < n; ++i)
   {
      m_workers.emplace_back([this]() {
         for(;;)
         {
            std::function<void()> task;
            {
               std::unique_lock<std::mutex> lk(m_mu);
               m_cv.wait(lk, [this]() { return m_stop || !m_q.empty(); });
               if(m_stop && m_q.empty()) return;
               task = std::move(m_q.front());
               m_q.pop();
            }
            try { task(); } catch(...) { /* swallow; future stores exception */ }
         }
      });
   }
}

ThreadPool::~ThreadPool()
{
   { std::lock_guard<std::mutex> lk(m_mu); m_stop = true; }
   m_cv.notify_all();
   for(auto& t : m_workers) if(t.joinable()) t.join();
}
