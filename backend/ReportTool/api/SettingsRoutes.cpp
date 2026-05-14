//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       SettingsRoutes.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "SettingsRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../core/Crypto.h"
#include "../core/TelegramClient.h"

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   //--- Mask a bot token like "12345...AbCd" (first 6 / last 4 chars).
   std::string MaskToken(const std::string& t)
   {
      if(t.size() <= 10) return std::string(t.size(), '*');
      return t.substr(0, 6) + std::string(t.size() - 10, '*') + t.substr(t.size() - 4);
   }
}

void SettingsRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/settings/telegram", [ctx](const httplib::Request&, httplib::Response& res){
      const std::string enc = SettingsRepo::Get(*ctx->db, "telegram_bot_token_encrypted");
      std::string token_masked;
      bool configured = false;
      if(!enc.empty())
      {
         std::string plain;
         if(Crypto::DecryptB64(enc, &plain) && !plain.empty())
         {
            token_masked = MaskToken(plain);
            configured = true;
         }
      }
      const std::string chat = SettingsRepo::Get(*ctx->db, "telegram_default_chat_id");
      res.set_content(json{
         { "configured",        configured },
         { "bot_token_masked",  token_masked },
         { "default_chat_id",   chat },
      }.dump(), "application/json");
   });

   srv.Put("/api/settings/telegram", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      //--- Only write token when a non-empty new value is provided, so PATCH-like flow:
      //--- empty token field keeps the existing one.
      if(j.contains("bot_token") && j["bot_token"].is_string()
         && !j["bot_token"].get<std::string>().empty())
      {
         const std::string plain = j["bot_token"].get<std::string>();
         const std::string enc = Crypto::EncryptB64(plain);
         if(enc.empty()) { SendError(res, 500, "encryption failed"); return; }
         SettingsRepo::Set(*ctx->db, "telegram_bot_token_encrypted", enc);
      }
      if(j.contains("default_chat_id") && j["default_chat_id"].is_string())
         SettingsRepo::Set(*ctx->db, "telegram_default_chat_id",
                           j["default_chat_id"].get<std::string>());
      res.set_content(R"({"ok":true})", "application/json");
   });

   //--- Test the configured bot+chat by sending a probe message.
   srv.Post("/api/settings/telegram/test", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      const std::string override_chat = (j.is_object() && j.contains("chat_id"))
                                          ? j["chat_id"].get<std::string>() : std::string();
      const std::string enc = SettingsRepo::Get(*ctx->db, "telegram_bot_token_encrypted");
      if(enc.empty()) { SendError(res, 400, "bot token not configured"); return; }
      std::string token;
      if(!Crypto::DecryptB64(enc, &token) || token.empty())
      { SendError(res, 500, "failed to decrypt bot token"); return; }
      const std::string chat = !override_chat.empty()
         ? override_chat
         : SettingsRepo::Get(*ctx->db, "telegram_default_chat_id");
      if(chat.empty()) { SendError(res, 400, "no chat_id (set global default or pass override)"); return; }

      const auto r = TelegramClient::SendMessage(token, chat,
         "✅ Test message from ReportTool — your bot is wired up correctly.");
      if(!r.ok) { SendError(res, 502, r.error); return; }
      res.set_content(json{ {"ok", true}, {"chat_id", chat} }.dump(), "application/json");
   });
}
