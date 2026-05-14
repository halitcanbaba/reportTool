//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       SessionManager.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "SessionManager.h"
#include "Crypto.h"
#include "Logger.h"
#include "../api/AppContext.h"
#include "../db/Repos.h"

SessionManager::SessionManager(AppContext* ctx) : m_ctx(ctx) {}
SessionManager::~SessionManager() { Stop(); }

void SessionManager::Start()
{
   if(m_th.joinable()) return;
   m_stop = false;
   m_th = std::thread([this]{ Loop(); });
}

void SessionManager::Stop()
{
   { std::lock_guard<std::mutex> lk(m_mu); m_stop = true; }
   m_cv.notify_all();
   if(m_th.joinable()) m_th.join();
}

void SessionManager::Loop()
{
   Logger::SetRequestId("sessions");
   m_ctx->log->Info("SessionManager thread started.");
   while(!m_stop)
   {
      try
      {
         const int64_t n = SessionRepo::DeleteExpired(*m_ctx->db, (int64_t)time(nullptr));
         if(n > 0) m_ctx->log->Info("Session cleanup: %lld expired rows removed", (long long)n);
      }
      catch(const std::exception& e)
      {
         m_ctx->log->Error("SessionManager cleanup: %s", e.what());
      }
      std::unique_lock<std::mutex> lk(m_mu);
      m_cv.wait_for(lk, std::chrono::minutes(5), [this]{ return m_stop.load(); });
   }
   m_ctx->log->Info("SessionManager thread exiting.");
   Logger::SetRequestId("");
}

std::string SessionManager::IssueSession(int64_t user_id,
                                          const std::string& remote_addr,
                                          const std::string& user_agent)
{
   const auto rand = Crypto::RandomBytes(32);
   if(rand.empty()) return "";
   Session s;
   s.token       = Crypto::Base64UrlEncode(rand.data(), rand.size());
   s.user_id     = user_id;
   s.created_at  = (int64_t)time(nullptr);
   s.expires_at  = s.created_at + kSessionTtlSec;
   s.remote_addr = remote_addr;
   //--- Truncate user_agent to keep DB row sane.
   s.user_agent  = user_agent.size() > 240 ? user_agent.substr(0, 240) : user_agent;
   if(!SessionRepo::Insert(*m_ctx->db, s)) return "";
   return s.token;
}

std::optional<std::pair<User, Session>>
SessionManager::ValidateAndExtend(const std::string& token)
{
   if(token.empty()) return std::nullopt;
   auto s = SessionRepo::Get(*m_ctx->db, token);
   if(!s) return std::nullopt;
   const int64_t now = (int64_t)time(nullptr);
   if(s->expires_at <= now)
   {
      SessionRepo::Delete(*m_ctx->db, token);
      return std::nullopt;
   }
   auto u = UserRepo::Get(*m_ctx->db, s->user_id);
   if(!u || !u->active)
   {
      SessionRepo::Delete(*m_ctx->db, token);
      return std::nullopt;
   }
   //--- Sliding window: extend expiry on every validation.
   const int64_t new_exp = now + kSessionTtlSec;
   SessionRepo::Touch(*m_ctx->db, token, new_exp);
   s->expires_at = new_exp;
   return std::make_pair(*u, *s);
}

void SessionManager::Invalidate(const std::string& token)
{
   if(token.empty()) return;
   SessionRepo::Delete(*m_ctx->db, token);
}

void SessionManager::RevokeUser(int64_t user_id)
{
   SessionRepo::DeleteByUser(*m_ctx->db, user_id);
}

void SessionManager::RevokeUserExcept(int64_t user_id, const std::string& keep_token)
{
   SessionRepo::DeleteByUserExcept(*m_ctx->db, user_id, keep_token);
}
