//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       DataLoader.cpp             |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "DataLoader.h"
#include "../core/TimeUtil.h"

namespace
{
   constexpr size_t  BATCH  = 200;
   constexpr int64_t WINDOW = 120LL * 86400LL;

   std::string W2U(LPCWSTR w)
   {
      if(!w || w[0]==L'\0') return "";
      int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
      if(n <= 1) return "";
      std::string s(n - 1, '\0');
      WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
      return s;
   }

   std::wstring U2W(const std::string& s)
   {
      if(s.empty()) return L"";
      int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
      std::wstring r(n, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &r[0], n);
      if(!r.empty() && r.back() == L'\0') r.pop_back();
      return r;
   }
}

std::vector<UserInfo>
DataLoader::LoadUsersByLogins(Connection& conn,
                              const std::vector<uint64_t>& logins,
                              Logger& log)
{
   std::vector<UserInfo> out;
   if(logins.empty()) return out;
   IMTManagerAPI* api = conn.Api();
   if(!api) return out;

   std::lock_guard<std::mutex> lock(conn.CallMutex());

   IMTUserArray* users = api->UserCreateArray();
   if(!users) { log.Error("UserCreateArray failed"); return out; }

   const size_t total = logins.size();
   log.Info("Fetching user details for %zu logins ...", total);

   for(size_t base = 0; base < total; base += BATCH)
   {
      const size_t count = std::min(BATCH, total - base);
      const uint64_t* batch = logins.data() + base;
      users->Clear();

      MTAPIRES res = api->UserRequestByLogins(batch, (uint32_t)count, users);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) log.Error("UserRequestByLogins batch=%zu: %u", base, res);
         continue;
      }

      const uint32_t n = users->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTUser* u = users->Next(i); if(!u) continue;
         UserInfo r;
         r.login        = u->Login();
         r.group        = W2U(u->Group());
         r.name         = W2U(u->Name());
         r.leverage     = u->Leverage();
         r.registration = u->Registration();
         out.push_back(std::move(r));
      }
   }

   users->Release();
   log.Info("Resolved %zu users by login", out.size());
   return out;
}

std::vector<UserInfo>
DataLoader::LoadUsers(Connection& conn,
                      const std::vector<std::string>& group_masks,
                      const std::string& group_regex,
                      uint64_t login_min, uint64_t login_max,
                      Logger& log)
{
   std::vector<UserInfo> out;
   IMTManagerAPI* api = conn.Api();
   if(!api) return out;

   std::vector<std::string> masks = group_masks;
   if(masks.empty()) masks.push_back("*");

   const bool use_regex = !group_regex.empty();
   std::regex group_re;
   if(use_regex)
   {
      try { group_re = std::regex(group_regex, std::regex::ECMAScript | std::regex::icase); }
      catch(const std::regex_error& e) { log.Error("Invalid group_regex: %s", e.what()); return out; }
   }

   std::lock_guard<std::mutex> lock(conn.CallMutex());

   IMTUserArray* users = api->UserCreateArray();
   if(!users) { log.Error("UserCreateArray failed"); return out; }

   std::unordered_set<uint64_t> seen;
   seen.reserve(8192);

   for(const auto& m : masks)
   {
      users->Clear();
      const std::wstring wmask = U2W(m);
      MTAPIRES res = api->UserRequestArray(wmask.c_str(), users);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) log.Error("UserRequestArray('%s'): %u", m.c_str(), res);
         continue;
      }
      const uint32_t n = users->Total();
      log.Info("  mask '%s' -> %u users", m.c_str(), n);

      for(uint32_t i = 0; i < n; ++i)
      {
         IMTUser* u = users->Next(i); if(!u) continue;
         const uint64_t login = u->Login();
         if(!seen.insert(login).second) continue;

         if(login_min && login < login_min) continue;
         if(login_max && login > login_max) continue;

         std::string group = W2U(u->Group());
         if(use_regex && !std::regex_search(group, group_re)) continue;

         UserInfo r;
         r.login        = login;
         r.group        = group;
         r.name         = W2U(u->Name());
         r.leverage     = u->Leverage();
         r.registration = u->Registration();
         out.push_back(std::move(r));
      }
   }
   users->Release();
   log.Info("LoadUsers: %zu users (after mask+regex+range)", out.size());
   return out;
}

namespace
{
   //--- One batch × one window of deals. Locks the connection mutex internally.
   struct DealBatchResult
   {
      std::unordered_map<uint64_t, std::vector<DealRow>> per_login;
      size_t deals = 0;
      bool   empty = false;
   };

   DealBatchResult FetchDealsBatchWindow(Connection* conn,
                                         const std::vector<uint64_t> logins,
                                         int64_t w_from, int64_t w_to,
                                         Logger* log)
   {
      DealBatchResult r;
      IMTManagerAPI* api = conn->Api();
      if(!api) return r;

      std::lock_guard<std::mutex> lock(conn->CallMutex());

      IMTDealArray* arr = api->DealCreateArray();
      if(!arr) { if(log) log->Error("DealCreateArray failed"); return r; }

      MTAPIRES res = api->DealRequestByLogins(logins.data(), (uint32_t)logins.size(), w_from, w_to, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) if(log) log->Warn("DealRequestByLogins window=%lld..%lld: %u", w_from, w_to, res);
         arr->Release();
         r.empty = (res == 13);
         return r;
      }

      const uint32_t n = arr->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTDeal* d = arr->Next(i); if(!d) continue;
         DealRow x;
         x.ticket     = d->Deal();
         x.login      = d->Login();
         x.position_id= d->PositionID();
         x.action     = d->Action();
         x.entry      = d->Entry();
         x.reason     = d->Reason();
         x.time       = d->Time();
         x.profit     = d->Profit();
         x.storage    = d->Storage();
         x.commission = d->Commission();
         x.fee        = d->Fee();
         x.volume     = d->Volume();
         x.price      = d->Price();
         x.symbol     = W2U(d->Symbol());
         x.comment    = W2U(d->Comment());
         r.per_login[x.login].push_back(std::move(x));
         r.deals++;
      }
      arr->Release();
      return r;
   }
}

std::unordered_map<uint64_t, std::vector<DealRow>>
DataLoader::LoadDealsParallel(Connection& conn, ThreadPool& pool,
                              const std::vector<uint64_t>& logins,
                              int64_t from, int64_t to,
                              Logger& log)
{
   std::unordered_map<uint64_t, std::vector<DealRow>> out;
   if(logins.empty() || from >= to) return out;

   //--- Fan out (batch × window) tasks
   std::vector<std::future<DealBatchResult>> futs;
   futs.reserve((logins.size() / BATCH + 1) * ((to - from) / WINDOW + 1));

   for(size_t base = 0; base < logins.size(); base += BATCH)
   {
      const size_t count = std::min(BATCH, logins.size() - base);
      std::vector<uint64_t> batch(logins.begin() + base, logins.begin() + base + count);

      for(int64_t w = from; w < to; w += WINDOW)
      {
         const int64_t w_to = std::min(w + WINDOW, to);
         Connection* cptr = &conn; Logger* lptr = &log;
         futs.push_back(pool.Submit([cptr, batch, w, w_to, lptr]() {
            return FetchDealsBatchWindow(cptr, batch, w, w_to, lptr);
         }));
      }
   }

   //--- Reduce
   std::set<uint64_t> seen_tickets;
   size_t total = 0;
   for(auto& f : futs)
   {
      DealBatchResult r;
      try { r = f.get(); } catch(const std::exception& e) { log.Warn("deal task: %s", e.what()); continue; }
      total += r.deals;
      for(auto& kv : r.per_login)
      {
         auto& dst = out[kv.first];
         for(auto& d : kv.second)
            if(seen_tickets.insert(d.ticket).second) dst.push_back(std::move(d));
      }
   }

   for(auto& kv : out)
      std::sort(kv.second.begin(), kv.second.end(),
                [](const DealRow& a, const DealRow& b){ return a.time < b.time; });

   log.Info("Deals fetched: %zu (after dedup, across %zu tasks)", total, futs.size());
   return out;
}

namespace
{
   struct DailyBatchResult
   {
      std::unordered_map<uint64_t, std::vector<DailyRow>> per_login;
      size_t rows = 0;
   };

   DailyBatchResult FetchDailyBatchWindow(Connection* conn,
                                          const std::vector<uint64_t> logins,
                                          int64_t w_from, int64_t w_to,
                                          Logger* log)
   {
      DailyBatchResult r;
      IMTManagerAPI* api = conn->Api();
      if(!api) return r;

      std::lock_guard<std::mutex> lock(conn->CallMutex());

      IMTDailyArray* arr = api->DailyCreateArray();
      if(!arr) { if(log) log->Error("DailyCreateArray failed"); return r; }

      MTAPIRES res = api->DailyRequestLightByLogins(logins.data(), (uint32_t)logins.size(),
                                                     w_from, w_to, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) if(log) log->Warn("DailyRequestByLogins window=%lld..%lld: %u", w_from, w_to, res);
         arr->Release();
         return r;
      }

      const uint32_t n = arr->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTDaily* d = arr->Next(i); if(!d) continue;
         DailyRow x;
         x.login         = d->Login();
         x.datetime      = d->Datetime();
         x.balance       = d->Balance();
         x.profit_equity = d->ProfitEquity();
         x.margin        = d->Margin();
         x.profit        = d->Profit();
         x.daily_profit  = d->DailyProfit();
         x.daily_balance = d->DailyBalance();
         x.daily_credit  = d->DailyCredit();
         x.daily_storage = d->DailyStorage();
         x.currency      = W2U(d->Currency());
         x.currency_digits = d->CurrencyDigits();
         r.per_login[x.login].push_back(std::move(x));
         r.rows++;
      }
      arr->Release();
      return r;
   }
}

std::unordered_map<uint64_t, std::vector<DailyRow>>
DataLoader::LoadDailyParallel(Connection& conn, ThreadPool& pool,
                              const std::vector<uint64_t>& logins,
                              int64_t from, int64_t to,
                              Logger& log)
{
   std::unordered_map<uint64_t, std::vector<DailyRow>> out;
   if(logins.empty() || from >= to) return out;

   std::vector<std::future<DailyBatchResult>> futs;
   for(size_t base = 0; base < logins.size(); base += BATCH)
   {
      const size_t count = std::min(BATCH, logins.size() - base);
      std::vector<uint64_t> batch(logins.begin() + base, logins.begin() + base + count);
      for(int64_t w = from; w < to; w += WINDOW)
      {
         const int64_t w_to = std::min(w + WINDOW, to);
         Connection* cptr = &conn; Logger* lptr = &log;
         futs.push_back(pool.Submit([cptr, batch, w, w_to, lptr]() {
            return FetchDailyBatchWindow(cptr, batch, w, w_to, lptr);
         }));
      }
   }

   std::set<std::pair<uint64_t,int64_t>> seen;
   size_t total = 0;
   for(auto& f : futs)
   {
      DailyBatchResult r;
      try { r = f.get(); } catch(const std::exception& e) { log.Warn("daily task: %s", e.what()); continue; }
      total += r.rows;
      for(auto& kv : r.per_login)
      {
         auto& dst = out[kv.first];
         for(auto& d : kv.second)
            if(seen.insert({d.login, d.datetime}).second) dst.push_back(std::move(d));
      }
   }

   for(auto& kv : out)
      std::sort(kv.second.begin(), kv.second.end(),
                [](const DailyRow& a, const DailyRow& b){ return a.datetime < b.datetime; });

   log.Info("Daily fetched: %zu rows (after dedup)", total);
   return out;
}

std::unordered_map<uint64_t, std::vector<DailyRow>>
DataLoader::LoadDailyBoundary(Connection& conn, ThreadPool& pool,
                              const std::vector<uint64_t>& logins,
                              const std::vector<int64_t>& target_times,
                              int margin_days,
                              Logger& log)
{
   std::unordered_map<uint64_t, std::vector<DailyRow>> out;
   if(target_times.empty() || logins.empty()) return out;

   const int64_t margin = (int64_t)margin_days * 86400LL;
   std::vector<int64_t> sorted = target_times;
   std::sort(sorted.begin(), sorted.end());
   std::vector<std::pair<int64_t,int64_t>> windows;
   for(int64_t t : sorted)
   {
      const int64_t wf = t - margin;
      const int64_t wt = t + 86400;
      if(!windows.empty() && wf <= windows.back().second)
         windows.back().second = std::max(windows.back().second, wt);
      else
         windows.push_back({wf, wt});
   }

   std::vector<std::future<DailyBatchResult>> futs;
   for(size_t base = 0; base < logins.size(); base += BATCH)
   {
      const size_t count = std::min(BATCH, logins.size() - base);
      std::vector<uint64_t> batch(logins.begin() + base, logins.begin() + base + count);
      for(const auto& w : windows)
      {
         Connection* cptr = &conn; Logger* lptr = &log;
         int64_t wf = w.first, wt = w.second;
         futs.push_back(pool.Submit([cptr, batch, wf, wt, lptr]() {
            return FetchDailyBatchWindow(cptr, batch, wf, wt, lptr);
         }));
      }
   }

   std::set<std::pair<uint64_t,int64_t>> seen;
   for(auto& f : futs)
   {
      DailyBatchResult r;
      try { r = f.get(); } catch(...) { continue; }
      for(auto& kv : r.per_login)
      {
         auto& dst = out[kv.first];
         for(auto& d : kv.second)
            if(seen.insert({d.login, d.datetime}).second) dst.push_back(std::move(d));
      }
   }

   for(auto& kv : out)
      std::sort(kv.second.begin(), kv.second.end(),
                [](const DailyRow& a, const DailyRow& b){ return a.datetime < b.datetime; });

   log.Info("Boundary daily: %zu logins covered", out.size());
   return out;
}
