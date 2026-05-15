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

   //--- Env-gated diagnostic for daily-snapshot date-boundary debugging.
   //--- Set REPORTTOOL_DAILY_DIAG=1 to enable [DAILY-DIAG] log lines.
   bool DailyDiagEnabled()
   {
      static const bool enabled = []() {
         char buf[8]; size_t n = 0;
         if(getenv_s(&n, buf, sizeof(buf), "REPORTTOOL_DAILY_DIAG") != 0) return false;
         return n > 0 && buf[0] == '1';
      }();
      return enabled;
   }

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

   //--- Populate UserInfo from an IMTUser*. SDK strings copied as UTF-8.
   void FillUserInfo(IMTUser* u, UserInfo& r)
   {
      r.login              = u->Login();
      r.group              = W2U(u->Group());
      r.name               = W2U(u->Name());
      r.first_name         = W2U(u->FirstName());
      r.last_name          = W2U(u->LastName());
      r.middle_name        = W2U(u->MiddleName());
      r.email              = W2U(u->EMail());
      r.phone              = W2U(u->Phone());
      r.country            = W2U(u->Country());
      r.state              = W2U(u->State());
      r.city               = W2U(u->City());
      r.zip_code           = W2U(u->ZIPCode());
      r.address            = W2U(u->Address());
      r.id                 = W2U(u->ID());
      r.company            = W2U(u->Company());
      r.account_tag        = W2U(u->Account());
      r.status             = W2U(u->Status());
      r.comment            = W2U(u->Comment());
      r.lead_campaign      = W2U(u->LeadCampaign());
      r.lead_source        = W2U(u->LeadSource());
      MTAPISTR ip{};
      r.last_ip            = W2U(u->LastIP(ip));
      r.agent              = u->Agent();
      r.client_id          = u->ClientID();
      r.leverage           = u->Leverage();
      r.language           = u->Language();
      r.color              = u->Color();
      r.limit_orders       = u->LimitOrders();
      r.rights             = u->Rights();
      r.registration       = u->Registration();
      r.last_access        = u->LastAccess();
      r.last_pass_change   = u->LastPassChange();
      r.balance                  = u->Balance();
      r.credit                   = u->Credit();
      r.interest_rate            = u->InterestRate();
      r.limit_positions_value    = u->LimitPositionsValue();
      r.commission_daily         = u->CommissionDaily();
      r.commission_monthly       = u->CommissionMonthly();
      r.commission_agent_daily   = u->CommissionAgentDaily();
      r.commission_agent_monthly = u->CommissionAgentMonthly();
      r.balance_prev_day         = u->BalancePrevDay();
      r.balance_prev_month       = u->BalancePrevMonth();
      r.equity_prev_day          = u->EquityPrevDay();
      r.equity_prev_month        = u->EquityPrevMonth();
   }

   void FillAccountInfo(IMTAccount* a, AccountInfo& r)
   {
      r.login              = a->Login();
      r.currency_digits    = a->CurrencyDigits();
      r.balance            = a->Balance();
      r.credit             = a->Credit();
      r.margin             = a->Margin();
      r.margin_free        = a->MarginFree();
      r.margin_level       = a->MarginLevel();
      r.margin_leverage    = a->MarginLeverage();
      r.margin_initial     = a->MarginInitial();
      r.margin_maintenance = a->MarginMaintenance();
      r.profit             = a->Profit();
      r.storage            = a->Storage();
      r.floating           = a->Floating();
      r.equity             = a->Equity();
      r.assets             = a->Assets();
      r.liabilities        = a->Liabilities();
      r.blocked_commission = a->BlockedCommission();
      r.blocked_profit     = a->BlockedProfit();
      r.so_activation      = a->SOActivation();
      r.so_time            = a->SOTime();
      r.so_level           = a->SOLevel();
      r.so_equity          = a->SOEquity();
      r.so_margin          = a->SOMargin();
   }

   void FillDealRow(IMTDeal* d, DealRow& x)
   {
      x.ticket             = d->Deal();
      x.login              = d->Login();
      x.order              = d->Order();
      x.dealer             = d->Dealer();
      x.position_id        = d->PositionID();
      x.expert_id          = d->ExpertID();
      x.action             = d->Action();
      x.entry              = d->Entry();
      x.reason             = d->Reason();
      x.digits             = d->Digits();
      x.digits_currency    = d->DigitsCurrency();
      x.flags              = d->Flags();
      x.modification_flags = d->ModificationFlags();
      x.time               = d->Time();
      x.time_msc           = d->TimeMsc();
      x.contract_size      = d->ContractSize();
      x.profit             = d->Profit();
      x.profit_raw         = d->ProfitRaw();
      x.storage            = d->Storage();
      x.commission         = d->Commission();
      x.fee                = d->Fee();
      x.value              = d->Value();
      x.volume             = d->Volume();
      x.volume_ext         = d->VolumeExt();
      x.volume_closed      = d->VolumeClosed();
      x.volume_closed_ext  = d->VolumeClosedExt();
      x.price              = d->Price();
      x.price_position     = d->PricePosition();
      x.price_sl           = d->PriceSL();
      x.price_tp           = d->PriceTP();
      x.price_gateway      = d->PriceGateway();
      x.market_bid         = d->MarketBid();
      x.market_ask         = d->MarketAsk();
      x.market_last        = d->MarketLast();
      x.tick_value         = d->TickValue();
      x.tick_size          = d->TickSize();
      x.rate_profit        = d->RateProfit();
      x.rate_margin        = d->RateMargin();
      x.symbol             = W2U(d->Symbol());
      x.comment            = W2U(d->Comment());
      x.gateway            = W2U(d->Gateway());
      x.external_id        = W2U(d->ExternalID());
   }

   void FillDailyRow(IMTDaily* d, DailyRow& x)
   {
      x.login              = d->Login();
      x.datetime           = d->Datetime();
      x.datetime_prev      = d->DatetimePrev();
      x.balance            = d->Balance();
      x.credit             = d->Credit();
      x.profit_equity      = d->ProfitEquity();
      x.margin             = d->Margin();
      x.margin_free        = d->MarginFree();
      x.margin_level       = d->MarginLevel();
      x.margin_leverage    = d->MarginLeverage();
      x.profit             = d->Profit();
      x.profit_storage     = d->ProfitStorage();
      x.profit_assets      = d->ProfitAssets();
      x.profit_liabilities = d->ProfitLiabilities();
      x.interest_rate      = d->InterestRate();
      x.commission_daily   = d->CommissionDaily();
      x.commission_monthly = d->CommissionMonthly();
      x.agent_daily        = d->AgentDaily();
      x.agent_monthly      = d->AgentMonthly();
      x.balance_prev_day   = d->BalancePrevDay();
      x.balance_prev_month = d->BalancePrevMonth();
      x.equity_prev_day    = d->EquityPrevDay();
      x.equity_prev_month  = d->EquityPrevMonth();
      x.daily_profit       = d->DailyProfit();
      x.daily_balance      = d->DailyBalance();
      x.daily_credit       = d->DailyCredit();
      x.daily_charge       = d->DailyCharge();
      x.daily_correction   = d->DailyCorrection();
      x.daily_bonus        = d->DailyBonus();
      x.daily_storage      = d->DailyStorage();
      x.daily_comm_instant = d->DailyCommInstant();
      x.daily_comm_round   = d->DailyCommRound();
      x.daily_agent        = d->DailyAgent();
      x.daily_interest     = d->DailyInterest();
      x.currency           = W2U(d->Currency());
      x.currency_digits    = d->CurrencyDigits();
   }

   void FillPositionRow(IMTPosition* p, PositionRow& x)
   {
      x.login              = p->Login();
      x.symbol             = W2U(p->Symbol());
      x.action             = p->Action();
      x.digits             = p->Digits();
      x.digits_currency    = p->DigitsCurrency();
      x.contract_size      = p->ContractSize();
      x.time_create        = p->TimeCreate();
      x.time_update        = p->TimeUpdate();
      x.price_open         = p->PriceOpen();
      x.price_current      = p->PriceCurrent();
      x.price_sl           = p->PriceSL();
      x.price_tp           = p->PriceTP();
      x.volume             = p->Volume();
      x.profit             = p->Profit();
      x.storage            = p->Storage();
      x.rate_profit        = p->RateProfit();
      x.rate_margin        = p->RateMargin();
      x.expert_id          = p->ExpertID();
      x.expert_position_id = p->ExpertPositionID();
      x.activation_mode    = p->ActivationMode();
      x.activation_time    = p->ActivationTime();
      x.activation_price   = p->ActivationPrice();
      x.activation_flags   = p->ActivationFlags();
      x.comment            = W2U(p->Comment());
   }

   void FillOrderRow(IMTOrder* o, OrderRow& x)
   {
      x.order              = o->Order();
      x.login              = o->Login();
      x.dealer             = o->Dealer();
      x.expert_id          = o->ExpertID();
      x.position_id        = o->PositionID();
      x.digits             = o->Digits();
      x.digits_currency    = o->DigitsCurrency();
      x.contract_size      = o->ContractSize();
      x.state              = o->State();
      x.reason             = o->Reason();
      x.type               = o->Type();
      x.type_fill          = o->TypeFill();
      x.type_time          = o->TypeTime();
      x.time_setup         = o->TimeSetup();
      x.time_setup_msc     = o->TimeSetupMsc();
      x.time_expiration    = o->TimeExpiration();
      x.time_done          = o->TimeDone();
      x.time_done_msc      = o->TimeDoneMsc();
      x.price_order        = o->PriceOrder();
      x.price_trigger      = o->PriceTrigger();
      x.price_current      = o->PriceCurrent();
      x.price_sl           = o->PriceSL();
      x.price_tp           = o->PriceTP();
      x.volume_initial     = o->VolumeInitial();
      x.volume_current     = o->VolumeCurrent();
      x.rate_margin        = o->RateMargin();
      x.activation_mode    = o->ActivationMode();
      x.activation_time    = o->ActivationTime();
      x.activation_price   = o->ActivationPrice();
      x.activation_flags   = o->ActivationFlags();
      x.symbol             = W2U(o->Symbol());
      x.comment            = W2U(o->Comment());
      x.external_id        = W2U(o->ExternalID());
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
         UserInfo r; FillUserInfo(u, r);
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

         UserInfo r; FillUserInfo(u, r);
         out.push_back(std::move(r));
      }
   }
   users->Release();
   log.Info("LoadUsers: %zu users (after mask+regex+range)", out.size());
   return out;
}

//+----------------- Accounts (IMTAccount bulk) ---------------------+

std::unordered_map<uint64_t, AccountInfo>
DataLoader::LoadAccountsByLogins(Connection& conn, ThreadPool& pool,
                                 const std::vector<uint64_t>& logins,
                                 Logger& log)
{
   std::unordered_map<uint64_t, AccountInfo> out;
   if(logins.empty()) return out;
   IMTManagerAPI* api = conn.Api();
   if(!api) return out;

   std::lock_guard<std::mutex> lock(conn.CallMutex());
   IMTAccountArray* arr = api->UserCreateAccountArray();
   if(!arr) { log.Error("UserCreateAccountArray failed"); return out; }

   const size_t total = logins.size();
   for(size_t base = 0; base < total; base += BATCH)
   {
      const size_t count = std::min(BATCH, total - base);
      arr->Clear();
      MTAPIRES res = api->UserAccountRequestByLogins(logins.data() + base, (uint32_t)count, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) log.Warn("UserAccountRequestByLogins batch=%zu: %u", base, res);
         continue;
      }
      const uint32_t n = arr->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTAccount* a = arr->Next(i); if(!a) continue;
         AccountInfo r; FillAccountInfo(a, r);
         out[r.login] = std::move(r);
      }
   }
   arr->Release();
   (void)pool;  // bulk per-batch is fast enough; no fan-out needed
   log.Info("Accounts fetched: %zu", out.size());
   return out;
}

namespace
{
   //--- One batch × one window of deals.
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
         DealRow x; FillDealRow(d, x);
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

      MTAPIRES res = api->DailyRequestByLogins(logins.data(), (uint32_t)logins.size(),
                                               w_from, w_to, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) if(log) log->Warn("DailyRequestByLogins window=%lld..%lld: %u", w_from, w_to, res);
         arr->Release();
         return r;
      }

      const uint32_t n = arr->Total();
      const bool diag = log && DailyDiagEnabled();
      if(diag)
      {
         log->Info("[DAILY-DIAG] window w_from=%lld (%s, %%86400=%lld) w_to=%lld (%s) logins=%zu rows=%u",
                   (long long)w_from, TimeUtil::FormatDateTime(w_from).c_str(),
                   (long long)(w_from % 86400),
                   (long long)w_to,   TimeUtil::FormatDateTime(w_to).c_str(),
                   logins.size(), n);
      }
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTDaily* d = arr->Next(i); if(!d) continue;
         DailyRow x; FillDailyRow(d, x);
         if(diag && (i < 3 || i + 3 >= n))
         {
            const int64_t dt = x.datetime;
            log->Info("[DAILY-DIAG]   row[%u/%u] login=%llu Datetime=%lld (%s, %%86400=%lld) ProfitEquity=%.4f EquityPrevDay=%.4f",
                      i, n, (unsigned long long)x.login,
                      (long long)dt, TimeUtil::FormatDateTime(dt).c_str(),
                      (long long)(dt % 86400),
                      x.profit_equity, x.equity_prev_day);
         }
         r.per_login[x.login].push_back(std::move(x));
         r.rows++;
      }
      if(diag && logins.size() <= 5)
      {
         for(uint64_t lg : logins)
         {
            auto it = r.per_login.find(lg);
            const size_t cnt = (it == r.per_login.end()) ? 0 : it->second.size();
            log->Info("[DAILY-DIAG]   per-login login=%llu rows=%zu",
                      (unsigned long long)lg, cnt);
         }
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

//+----------------- Positions (IMTPosition bulk) -------------------+

std::unordered_map<uint64_t, std::vector<PositionRow>>
DataLoader::LoadPositionsByLogins(Connection& conn, ThreadPool& pool,
                                  const std::vector<uint64_t>& logins,
                                  Logger& log)
{
   std::unordered_map<uint64_t, std::vector<PositionRow>> out;
   if(logins.empty()) return out;
   IMTManagerAPI* api = conn.Api();
   if(!api) return out;

   std::lock_guard<std::mutex> lock(conn.CallMutex());
   IMTPositionArray* arr = api->PositionCreateArray();
   if(!arr) { log.Error("PositionCreateArray failed"); return out; }

   const size_t total = logins.size();
   size_t rows = 0;
   for(size_t base = 0; base < total; base += BATCH)
   {
      const size_t count = std::min(BATCH, total - base);
      arr->Clear();
      MTAPIRES res = api->PositionRequestByLogins(logins.data() + base, (uint32_t)count, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) log.Warn("PositionRequestByLogins batch=%zu: %u", base, res);
         continue;
      }
      const uint32_t n = arr->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTPosition* p = arr->Next(i); if(!p) continue;
         PositionRow r; FillPositionRow(p, r);
         out[r.login].push_back(std::move(r));
         rows++;
      }
   }
   arr->Release();
   (void)pool;
   log.Info("Positions fetched: %zu rows across %zu logins", rows, out.size());
   return out;
}

//+----------------- Open Orders (IMTOrder bulk) --------------------+

std::unordered_map<uint64_t, std::vector<OpenOrderRow>>
DataLoader::LoadOpenOrdersByLogins(Connection& conn, ThreadPool& pool,
                                   const std::vector<uint64_t>& logins,
                                   Logger& log)
{
   std::unordered_map<uint64_t, std::vector<OpenOrderRow>> out;
   if(logins.empty()) return out;
   IMTManagerAPI* api = conn.Api();
   if(!api) return out;

   std::lock_guard<std::mutex> lock(conn.CallMutex());
   IMTOrderArray* arr = api->OrderCreateArray();
   if(!arr) { log.Error("OrderCreateArray failed"); return out; }

   const size_t total = logins.size();
   size_t rows = 0;
   for(size_t base = 0; base < total; base += BATCH)
   {
      const size_t count = std::min(BATCH, total - base);
      arr->Clear();
      MTAPIRES res = api->OrderRequestByLogins(logins.data() + base, (uint32_t)count, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) log.Warn("OrderRequestByLogins batch=%zu: %u", base, res);
         continue;
      }
      const uint32_t n = arr->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTOrder* o = arr->Next(i); if(!o) continue;
         OpenOrderRow r; FillOrderRow(o, r);
         out[r.login].push_back(std::move(r));
         rows++;
      }
   }
   arr->Release();
   (void)pool;
   log.Info("Open orders fetched: %zu rows across %zu logins", rows, out.size());
   return out;
}

//+----------------- Order History (IMTOrder range parallel) --------+

namespace
{
   struct OrderHistoryBatchResult
   {
      std::unordered_map<uint64_t, std::vector<HistoryOrderRow>> per_login;
      size_t rows = 0;
   };

   OrderHistoryBatchResult FetchOrderHistoryBatchWindow(Connection* conn,
                                                       const std::vector<uint64_t> logins,
                                                       int64_t w_from, int64_t w_to,
                                                       Logger* log)
   {
      OrderHistoryBatchResult r;
      IMTManagerAPI* api = conn->Api();
      if(!api) return r;

      std::lock_guard<std::mutex> lock(conn->CallMutex());
      IMTOrderArray* arr = api->OrderCreateArray();
      if(!arr) { if(log) log->Error("OrderCreateArray failed"); return r; }

      MTAPIRES res = api->HistoryRequestByLogins(logins.data(), (uint32_t)logins.size(),
                                                 w_from, w_to, arr);
      if(res != MT_RET_OK && res != MT_RET_OK_NONE)
      {
         if(res != 13) if(log) log->Warn("HistoryRequestByLogins window=%lld..%lld: %u", w_from, w_to, res);
         arr->Release();
         return r;
      }

      const uint32_t n = arr->Total();
      for(uint32_t i = 0; i < n; ++i)
      {
         IMTOrder* o = arr->Next(i); if(!o) continue;
         HistoryOrderRow x; FillOrderRow(o, x);
         r.per_login[x.login].push_back(std::move(x));
         r.rows++;
      }
      arr->Release();
      return r;
   }
}

std::unordered_map<uint64_t, std::vector<HistoryOrderRow>>
DataLoader::LoadOrderHistoryParallel(Connection& conn, ThreadPool& pool,
                                     const std::vector<uint64_t>& logins,
                                     int64_t from, int64_t to,
                                     Logger& log)
{
   std::unordered_map<uint64_t, std::vector<HistoryOrderRow>> out;
   if(logins.empty() || from >= to) return out;

   std::vector<std::future<OrderHistoryBatchResult>> futs;
   for(size_t base = 0; base < logins.size(); base += BATCH)
   {
      const size_t count = std::min(BATCH, logins.size() - base);
      std::vector<uint64_t> batch(logins.begin() + base, logins.begin() + base + count);
      for(int64_t w = from; w < to; w += WINDOW)
      {
         const int64_t w_to = std::min(w + WINDOW, to);
         Connection* cptr = &conn; Logger* lptr = &log;
         futs.push_back(pool.Submit([cptr, batch, w, w_to, lptr]() {
            return FetchOrderHistoryBatchWindow(cptr, batch, w, w_to, lptr);
         }));
      }
   }

   std::set<uint64_t> seen_orders;
   size_t total = 0;
   for(auto& f : futs)
   {
      OrderHistoryBatchResult r;
      try { r = f.get(); } catch(const std::exception& e) { log.Warn("order-history task: %s", e.what()); continue; }
      total += r.rows;
      for(auto& kv : r.per_login)
      {
         auto& dst = out[kv.first];
         for(auto& o : kv.second)
            if(seen_orders.insert(o.order).second) dst.push_back(std::move(o));
      }
   }

   for(auto& kv : out)
      std::sort(kv.second.begin(), kv.second.end(),
                [](const OrderRow& a, const OrderRow& b){
                   const int64_t ta = a.time_done ? a.time_done : a.time_setup;
                   const int64_t tb = b.time_done ? b.time_done : b.time_setup;
                   return ta < tb;
                });

   log.Info("Order history fetched: %zu (after dedup, across %zu tasks)", total, futs.size());
   return out;
}
