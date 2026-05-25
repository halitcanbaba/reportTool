//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       ChromeRenderer.cpp         |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "ChromeRenderer.h"
#include <windows.h>
#include <sys/stat.h>
#include <cstdio>

namespace
{
   //--- Probe the known install locations for Chromium-family browsers.
   //--- Chrome first (most common), then Edge (always on Win10+). Both
   //--- expose the same --headless --screenshot=FILE URL CLI.
   std::string FindBrowserPath()
   {
      static const char* candidates[] = {
         "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
         "C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe",
         "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
         "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
      };
      for(const char* p : candidates)
      {
         struct _stat64 st;
         if(_stat64(p, &st) == 0) return p;
      }
      return "";
   }
}

bool ChromeRenderer::Available()
{
   return !FindBrowserPath().empty();
}

bool ChromeRenderer::RenderPng(const std::string& url,
                                const std::string& out_path,
                                int viewport_w, int viewport_h,
                                int timeout_sec)
{
   const std::string browser = FindBrowserPath();
   if(browser.empty()) return false;

   //--- Delete any stale output so a 0-byte file from a previous failure
   //--- can't be mistaken for a fresh screenshot below.
   _unlink(out_path.c_str());

   //--- Build the command line. Single string for CreateProcess; the
   //--- browser argv quoting follows Win32 conventions: each argument
   //--- with spaces / special chars wrapped in double quotes.
   //---
   //--- Flags chosen for unattended captures:
   //---   --headless=new            modern headless mode (Chrome 109+)
   //---   --disable-gpu             headless on Windows needs this
   //---   --hide-scrollbars         no chrome around the page
   //---   --no-sandbox              required when running as a service / no user namespace
   //---   --window-size=W,H         viewport / fold line for the PNG
   //---   --virtual-time-budget=Ms  let the SPA finish fetch + paint
   //---   --screenshot=PATH         output target
   //---   --user-data-dir=…         fresh profile so cached state can't leak in/out
   char tmp_profile[MAX_PATH];
   if(!GetTempPathA(MAX_PATH, tmp_profile)) tmp_profile[0] = '\0';
   const std::string profile_dir = std::string(tmp_profile) + "rt_screenshot_profile";
   CreateDirectoryA(profile_dir.c_str(), nullptr);    // ok if exists

   char wsz[32]; std::snprintf(wsz, sizeof(wsz), "%d,%d", viewport_w, viewport_h);

   std::string cmd;
   cmd += "\""; cmd += browser; cmd += "\"";
   cmd += " --headless=new";
   cmd += " --disable-gpu";
   cmd += " --hide-scrollbars";
   cmd += " --no-sandbox";
   cmd += " --disable-extensions";
   cmd += " --window-size="; cmd += wsz;
   cmd += " --virtual-time-budget=10000";
   cmd += " --user-data-dir=\""; cmd += profile_dir; cmd += "\"";
   cmd += " --screenshot=\""; cmd += out_path; cmd += "\"";
   cmd += " \""; cmd += url; cmd += "\"";

   //--- CreateProcessA wants a mutable buffer for the command line.
   std::string mutable_cmd = cmd;
   STARTUPINFOA si{}; si.cb = sizeof(si);
   PROCESS_INFORMATION pi{};
   //--- DETACHED_PROCESS so the browser doesn't try to open a console
   //--- window (relevant when the parent is itself a service).
   if(!CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE,
                      DETACHED_PROCESS | CREATE_NO_WINDOW,
                      nullptr, nullptr, &si, &pi))
   {
      return false;
   }

   const DWORD wait = WaitForSingleObject(pi.hProcess, (DWORD)timeout_sec * 1000);
   if(wait == WAIT_TIMEOUT)
   {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 2000);
   }
   CloseHandle(pi.hThread);
   CloseHandle(pi.hProcess);

   //--- Did the browser actually write a non-empty PNG?
   struct _stat64 st;
   if(_stat64(out_path.c_str(), &st) != 0) return false;
   return st.st_size > 0;
}
