//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                  ThreadPool.h - Fixed-N worker pool              |
//+------------------------------------------------------------------+
#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>

class ThreadPool
{
public:
   explicit ThreadPool(size_t n);
   ~ThreadPool();

   template<class F, class... Args>
   auto Submit(F&& f, Args&&... args)
       -> std::future<typename std::invoke_result<F, Args...>::type>
   {
      using R = typename std::invoke_result<F, Args...>::type;
      auto task = std::make_shared<std::packaged_task<R()>>(
         std::bind(std::forward<F>(f), std::forward<Args>(args)...));
      std::future<R> fut = task->get_future();
      {
         std::lock_guard<std::mutex> lk(m_mu);
         if(m_stop) throw std::runtime_error("ThreadPool stopped");
         m_q.emplace([task]() { (*task)(); });
      }
      m_cv.notify_one();
      return fut;
   }

   size_t Size() const { return m_workers.size(); }

private:
   std::vector<std::thread>          m_workers;
   std::queue<std::function<void()>> m_q;
   std::mutex                        m_mu;
   std::condition_variable           m_cv;
   std::atomic<bool>                 m_stop{false};
};
