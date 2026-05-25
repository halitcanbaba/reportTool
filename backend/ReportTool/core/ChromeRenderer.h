//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|     ChromeRenderer.h - headless Chrome/Edge PNG screenshotter    |
//+------------------------------------------------------------------+
//--- Locates a Chromium-family browser binary on disk and shells out to
//--- its `--headless --screenshot=FILE URL` mode to capture the actual
//--- rendered SPA — including the result table styled exactly like the
//--- in-browser view. Used by the scheduler's "image" delivery format.
//--- Windows-only for now (the project is Windows-bound).
//+------------------------------------------------------------------+
#pragma once
#include <string>

namespace ChromeRenderer
{
   //--- True when a usable browser is on disk. Cheap path probe; safe to
   //--- call at module-init time.
   bool Available();

   //--- Render the URL into a PNG file at out_path. Returns true on success
   //--- (PNG exists and is non-empty). Times out after timeout_sec —
   //--- defaults to 30s which covers a slow MT5 result page load plus the
   //--- SPA's data fetch + render.
   bool RenderPng(const std::string& url,
                  const std::string& out_path,
                  int viewport_w = 1600,
                  int viewport_h = 1024,
                  int timeout_sec = 30);
}
