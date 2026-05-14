//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     TelegramClient.h - WinHTTP-based Telegram Bot API client     |
//+------------------------------------------------------------------+
#pragma once
#include <string>

namespace TelegramClient
{
   //--- Result of an API call. ok=true on HTTP 200 + JSON "ok":true.
   struct Result
   {
      bool        ok = false;
      int         http_status = 0;
      std::string error;          // human-readable failure reason (empty on success)
   };

   //--- POST https://api.telegram.org/bot<token>/sendMessage
   Result SendMessage(const std::string& bot_token,
                      const std::string& chat_id,
                      const std::string& text);

   //--- POST https://api.telegram.org/bot<token>/sendDocument
   //--- Streams `file_path` as multipart form data (≤ 50 MB per Telegram).
   //--- `caption` shown above the document in the chat (optional, ≤ 1024 chars).
   Result SendDocument(const std::string& bot_token,
                       const std::string& chat_id,
                       const std::string& file_path,
                       const std::string& display_name,
                       const std::string& caption);
}
