//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                         Logger.cpp               |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Logger.h"

namespace { thread_local std::string t_request_id; }

void Logger::SetRequestId(const std::string& id) { t_request_id = id; }
std::string Logger::RequestId() { return t_request_id; }

Logger::Logger(const std::string& log_path)
{
   fopen_s(&m_file, log_path.c_str(), "a");
}

Logger::~Logger()
{
   if(m_file) fclose(m_file);
}

void Logger::Info(const char* fmt, ...)
{
   va_list args; va_start(args, fmt); Log(LogLevel::INFO, fmt, args); va_end(args);
}

void Logger::Warn(const char* fmt, ...)
{
   va_list args; va_start(args, fmt); Log(LogLevel::WARN, fmt, args); va_end(args);
}

void Logger::Error(const char* fmt, ...)
{
   va_list args; va_start(args, fmt); Log(LogLevel::ERR, fmt, args); va_end(args);
}

void Logger::Banner(const char* title)
{
   const char* sep = "================================================================";
   Info(""); Info("%s", sep); Info("  %s", title); Info("%s", sep);
}

void Logger::Log(LogLevel level, const char* fmt, va_list args)
{
   char ts[32]; Timestamp(ts, sizeof(ts));
   char msg[4096]; vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
   const char* lvl = LevelStr(level);

   std::lock_guard<std::mutex> lock(m_mutex);

   if(t_request_id.empty())
   {
      printf("[%s][%s] %s\n", ts, lvl, msg);
      if(m_file) { fprintf(m_file, "[%s][%s] %s\n", ts, lvl, msg); fflush(m_file); }
   }
   else
   {
      printf("[%s][%s][%s] %s\n", ts, lvl, t_request_id.c_str(), msg);
      if(m_file) { fprintf(m_file, "[%s][%s][%s] %s\n", ts, lvl, t_request_id.c_str(), msg); fflush(m_file); }
   }
}

const char* Logger::LevelStr(LogLevel level)
{
   switch(level)
   {
      case LogLevel::INFO: return "INFO";
      case LogLevel::WARN: return "WARN";
      case LogLevel::ERR:  return "ERR ";
      default:             return "????";
   }
}

void Logger::Timestamp(char* buf, size_t size)
{
   time_t t = time(nullptr);
   struct tm tm_info; localtime_s(&tm_info, &t);
   strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);
}
