//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                  stdafx.h - Precompiled header   |
//+------------------------------------------------------------------+
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <cinttypes>

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <deque>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <limits>
#include <regex>
#include <atomic>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <future>
#include <chrono>
#include <functional>
#include <optional>

//--- MT5 SDK
#include "C:/MetaTrader5SDK/Include/MT5APIManager.h"

//--- Vendored single-header deps (fetched into third_party/)
#include "third_party/json.hpp"
