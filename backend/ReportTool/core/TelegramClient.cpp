//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       TelegramClient.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "TelegramClient.h"
#include "../third_party/json.hpp"
#include <winhttp.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

using nlohmann::json;

namespace
{
   struct HttpResponse
   {
      DWORD       status = 0;
      std::string body;
      std::string error;
   };

   std::wstring U2W(const std::string& s)
   {
      if(s.empty()) return L"";
      int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
      std::wstring r(n, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &r[0], n);
      if(!r.empty() && r.back() == L'\0') r.pop_back();
      return r;
   }

   //--- POST to api.telegram.org with arbitrary Content-Type body.
   HttpResponse PostHttps(const std::string& bot_token,
                          const std::string& path_after_bot,
                          const std::string& content_type,
                          const std::string& body)
   {
      HttpResponse r;
      HINTERNET hSession = WinHttpOpen(L"ReportTool/1.0",
         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      if(!hSession) { r.error = "WinHttpOpen failed"; return r; }

      HINTERNET hConnect = WinHttpConnect(hSession, L"api.telegram.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
      if(!hConnect) { r.error = "WinHttpConnect failed"; WinHttpCloseHandle(hSession); return r; }

      const std::string url_path = std::string("/bot") + bot_token + "/" + path_after_bot;
      HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", U2W(url_path).c_str(),
         nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      if(!hRequest)
      {
         r.error = "WinHttpOpenRequest failed";
         WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
         return r;
      }

      const std::wstring hdr_w = U2W("Content-Type: " + content_type);
      if(!WinHttpSendRequest(hRequest, hdr_w.c_str(), (DWORD)-1L,
            (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0))
      {
         char buf[64]; snprintf(buf, sizeof(buf), "WinHttpSendRequest err=%lu", GetLastError());
         r.error = buf;
      }
      else if(!WinHttpReceiveResponse(hRequest, nullptr))
      {
         char buf[64]; snprintf(buf, sizeof(buf), "WinHttpReceiveResponse err=%lu", GetLastError());
         r.error = buf;
      }
      else
      {
         DWORD status = 0, status_size = sizeof(status);
         WinHttpQueryHeaders(hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
         r.status = status;

         DWORD avail = 0;
         while(WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
         {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if(!WinHttpReadData(hRequest, chunk.data(), avail, &read)) break;
            chunk.resize(read);
            r.body += chunk;
         }
      }

      WinHttpCloseHandle(hRequest);
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return r;
   }

   std::string UrlEncode(const std::string& s)
   {
      static const char* hex = "0123456789ABCDEF";
      std::string out;
      out.reserve(s.size());
      for(unsigned char c : s)
      {
         if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
         else
         {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
         }
      }
      return out;
   }

   //--- Inspect Telegram JSON response: { "ok": bool, "description": str }.
   TelegramClient::Result Interpret(const HttpResponse& http)
   {
      TelegramClient::Result r;
      r.http_status = (int)http.status;
      if(!http.error.empty()) { r.error = http.error; return r; }
      if(http.status == 0)    { r.error = "no http status"; return r; }
      auto j = json::parse(http.body, nullptr, false);
      if(j.is_discarded())
      {
         r.error = "non-JSON response (HTTP " + std::to_string(r.http_status) + ")";
         return r;
      }
      const bool ok = j.value("ok", false);
      if(!ok)
      {
         r.error = "Telegram: " + j.value("description", std::string("(no description)"));
         return r;
      }
      r.ok = true;
      return r;
   }
}

TelegramClient::Result
TelegramClient::SendMessage(const std::string& bot_token,
                             const std::string& chat_id,
                             const std::string& text)
{
   const std::string body = "chat_id=" + UrlEncode(chat_id) + "&text=" + UrlEncode(text);
   return Interpret(PostHttps(bot_token, "sendMessage",
                              "application/x-www-form-urlencoded; charset=utf-8", body));
}

namespace
{
   //--- Build a single-file multipart body. `file_field` is the API's expected
   //--- form-field name (e.g. "document" or "photo").
   std::string BuildMultipartBody(const std::string& chat_id,
                                  const std::string& caption,
                                  const std::string& file_field,
                                  const std::string& display_name,
                                  const std::string& mime,
                                  const std::string& bytes,
                                  const std::string& boundary)
   {
      std::string body;
      body.reserve(bytes.size() + 4096);
      auto field = [&](const std::string& name, const std::string& value){
         body += "--" + boundary + "\r\n";
         body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
         body += value + "\r\n";
      };
      field("chat_id", chat_id);
      if(!caption.empty()) field("caption", caption);

      body += "--" + boundary + "\r\n";
      body += "Content-Disposition: form-data; name=\"" + file_field
            + "\"; filename=\"" + display_name + "\"\r\n";
      body += "Content-Type: " + mime + "\r\n\r\n";
      body += bytes;
      body += "\r\n";
      body += "--" + boundary + "--\r\n";
      return body;
   }
}

TelegramClient::Result
TelegramClient::SendDocumentBytes(const std::string& bot_token,
                                   const std::string& chat_id,
                                   const std::string& bytes,
                                   const std::string& display_name,
                                   const std::string& mime,
                                   const std::string& caption)
{
   const std::string boundary = "----ReportToolFormBoundaryXp0VYAvCq2mD3";
   const std::string body = BuildMultipartBody(chat_id, caption,
      "document", display_name, mime, bytes, boundary);
   return Interpret(PostHttps(bot_token, "sendDocument",
                              "multipart/form-data; boundary=" + boundary, body));
}

TelegramClient::Result
TelegramClient::SendPhotoBytes(const std::string& bot_token,
                                const std::string& chat_id,
                                const std::string& bytes,
                                const std::string& display_name,
                                const std::string& caption)
{
   //--- Telegram inspects the bytes for PNG/JPEG; the Content-Type we send is
   //--- mostly informational. image/png is a safe default for our screenshots.
   const std::string boundary = "----ReportToolFormBoundaryXp0VYAvCq2mD3";
   const std::string body = BuildMultipartBody(chat_id, caption,
      "photo", display_name, "image/png", bytes, boundary);
   return Interpret(PostHttps(bot_token, "sendPhoto",
                              "multipart/form-data; boundary=" + boundary, body));
}

TelegramClient::Result
TelegramClient::SendDocument(const std::string& bot_token,
                              const std::string& chat_id,
                              const std::string& file_path,
                              const std::string& display_name,
                              const std::string& caption)
{
   std::ifstream f(file_path, std::ios::binary);
   if(!f) { Result r; r.error = "cannot open " + file_path; return r; }
   std::stringstream ss; ss << f.rdbuf();
   return SendDocumentBytes(bot_token, chat_id, ss.str(),
                             display_name, "text/csv", caption);
}
