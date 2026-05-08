//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                  Logger.h - Structured logging   |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <cstdio>
#include <ctime>
#include <mutex>

enum class LogLevel { INFO, WARN, ERR };

class Logger
{
public:
   explicit Logger(const std::string& log_path);
   ~Logger();

   void Info (const char* fmt, ...);
   void Warn (const char* fmt, ...);
   void Error(const char* fmt, ...);
   void Banner(const char* title);
   void Log(LogLevel level, const char* fmt, va_list args);

   //--- thread-local request-id prefix; set per HTTP/job thread for traceability
   static void SetRequestId(const std::string& id);
   static std::string RequestId();

private:
   FILE*      m_file = nullptr;
   std::mutex m_mutex;

   static const char* LevelStr(LogLevel level);
   static void        Timestamp(char* buf, size_t size);
};
