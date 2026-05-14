//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|   SessionManager.h - issue/validate sessions + expiry cleanup    |
//+------------------------------------------------------------------+
#pragma once
#include "../core/Records.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <utility>

struct AppContext;

class SessionManager
{
public:
   explicit SessionManager(AppContext* ctx);
   ~SessionManager();

   void Start();
   void Stop();

   //--- Cookie lifetime: 7 days, refreshed on every successful validation.
   static constexpr int64_t kSessionTtlSec = 7 * 24 * 3600;

   //--- Mint a fresh session for the user, persist to DB, return the token
   //--- (URL-safe base64 of 32 random bytes). Empty string on failure.
   std::string IssueSession(int64_t user_id,
                            const std::string& remote_addr,
                            const std::string& user_agent);

   //--- Look up token, drop if expired/missing, otherwise slide expiry to
   //--- now + kSessionTtlSec and return {user, session}. nullopt if the
   //--- user is inactive or the token is unknown.
   std::optional<std::pair<User, Session>> ValidateAndExtend(const std::string& token);

   //--- Drop one session row (used by /api/auth/logout).
   void Invalidate(const std::string& token);

   //--- Drop all sessions for the user (used after admin password reset).
   void RevokeUser(int64_t user_id);

   //--- Drop all sessions for the user EXCEPT keep_token (used after the
   //--- user changes their own password — keeps the caller logged in).
   void RevokeUserExcept(int64_t user_id, const std::string& keep_token);

private:
   AppContext*               m_ctx;
   std::thread               m_th;
   std::atomic<bool>         m_stop{false};
   std::condition_variable   m_cv;
   std::mutex                m_mu;

   void Loop();
};
