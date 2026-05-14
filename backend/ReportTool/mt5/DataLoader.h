//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     DataLoader.h - parallel fetch of users/deals/daily/etc.     |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include "../core/Logger.h"
#include "../core/ThreadPool.h"
#include "Connection.h"
#include <unordered_map>
#include <vector>
#include <memory>

namespace DataLoader
{
   //--- Mask + regex + login range filtered fetch via UserRequestArray (per mask)
   //--- and optional client-side narrowing.
   std::vector<UserInfo>
   LoadUsers(Connection& conn,
             const std::vector<std::string>& group_masks,
             const std::string& group_regex,
             uint64_t login_min, uint64_t login_max,
             Logger& log);

   //--- Direct lookup by login list.
   std::vector<UserInfo>
   LoadUsersByLogins(Connection& conn,
                     const std::vector<uint64_t>& logins,
                     Logger& log);

   //--- Bulk live account state (IMTAccount) via UserAccountRequestByLogins.
   std::unordered_map<uint64_t, AccountInfo>
   LoadAccountsByLogins(Connection& conn, ThreadPool& pool,
                        const std::vector<uint64_t>& logins,
                        Logger& log);

   //--- Parallel deal fetch: 200-login batches × 120-day windows fanned out
   //--- across the supplied thread pool. Each task takes the connection's
   //--- CallMutex() to serialize SDK calls.
   std::unordered_map<uint64_t, std::vector<DealRow>>
   LoadDealsParallel(Connection& conn, ThreadPool& pool,
                     const std::vector<uint64_t>& logins,
                     int64_t from, int64_t to,
                     Logger& log);

   //--- Parallel daily fetch (heavy variant — populates the full DailyRow).
   std::unordered_map<uint64_t, std::vector<DailyRow>>
   LoadDailyParallel(Connection& conn, ThreadPool& pool,
                     const std::vector<uint64_t>& logins,
                     int64_t from, int64_t to,
                     Logger& log);

   //--- Sparse boundary fetch around target dates only — for floating-PL
   //--- start/end snapshots.
   std::unordered_map<uint64_t, std::vector<DailyRow>>
   LoadDailyBoundary(Connection& conn, ThreadPool& pool,
                     const std::vector<uint64_t>& logins,
                     const std::vector<int64_t>& target_times,
                     int margin_days,
                     Logger& log);

   //--- Bulk currently-open positions (IMTPosition) via PositionRequestByLogins.
   std::unordered_map<uint64_t, std::vector<PositionRow>>
   LoadPositionsByLogins(Connection& conn, ThreadPool& pool,
                         const std::vector<uint64_t>& logins,
                         Logger& log);

   //--- Bulk currently-working orders (IMTOrder open) via OrderRequestByLogins.
   std::unordered_map<uint64_t, std::vector<OpenOrderRow>>
   LoadOpenOrdersByLogins(Connection& conn, ThreadPool& pool,
                          const std::vector<uint64_t>& logins,
                          Logger& log);

   //--- Parallel order history fetch: 200-login batches × 120-day windows.
   std::unordered_map<uint64_t, std::vector<HistoryOrderRow>>
   LoadOrderHistoryParallel(Connection& conn, ThreadPool& pool,
                            const std::vector<uint64_t>& logins,
                            int64_t from, int64_t to,
                            Logger& log);
}
