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
   //--- `parse_mode` controls Telegram-side formatting: "" (plain),
   //--- "HTML" (<b>/<i>/<code>/<pre>/<a> subset), or "MarkdownV2".
   //--- Telegram caps messages at 4096 chars; callers truncate ahead.
   Result SendMessage(const std::string& bot_token,
                      const std::string& chat_id,
                      const std::string& text,
                      const std::string& parse_mode = "");

   //--- POST https://api.telegram.org/bot<token>/sendDocument
   //--- Streams `file_path` as multipart form data (≤ 50 MB per Telegram).
   //--- `caption` shown above the document in the chat (optional, ≤ 1024 chars).
   //--- Defaults Content-Type to text/csv to preserve the scheduler's behavior.
   Result SendDocument(const std::string& bot_token,
                       const std::string& chat_id,
                       const std::string& file_path,
                       const std::string& display_name,
                       const std::string& caption);

   //--- In-memory variant of sendDocument — takes bytes directly so callers
   //--- handling multipart uploads don't have to write to disk first.
   //--- `mime` is the document's Content-Type (e.g. "application/pdf").
   Result SendDocumentBytes(const std::string& bot_token,
                            const std::string& chat_id,
                            const std::string& bytes,
                            const std::string& display_name,
                            const std::string& mime,
                            const std::string& caption);

   //--- POST https://api.telegram.org/bot<token>/sendPhoto with an in-memory
   //--- image (PNG/JPEG). Telegram caps photos at 10 MB.
   Result SendPhotoBytes(const std::string& bot_token,
                         const std::string& chat_id,
                         const std::string& bytes,
                         const std::string& display_name,
                         const std::string& caption);
}
