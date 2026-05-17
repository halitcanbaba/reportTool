//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       ScheduleRoutes.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "ScheduleRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../core/Scheduler.h"
#include <algorithm>
#include <set>

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   json ToJson(const ScheduleEntry& s)
   {
      return json{
         { "id",                s.id },
         { "name",              s.name },
         { "ready_made_id",     s.ready_made_id },
         { "frequency",         s.frequency },
         { "time_hour",         s.time_hour },
         { "time_minute",       s.time_minute },
         { "day_of_week",       s.day_of_week },
         { "day_of_month",      s.day_of_month },
         { "every_n_hours",     s.every_n_hours },
         { "telegram_chat_id",  s.telegram_chat_id },
         { "delivery_format",   s.delivery_format.empty() ? std::string("csv") : s.delivery_format },
         { "enabled",           s.enabled },
         { "folder_id",         s.folder_id ? json(s.folder_id) : json(nullptr) },
         { "hours",             s.hours },
         { "days_of_week",      s.days_of_week },
         { "next_run_at",       s.next_run_at },
         { "last_run_at",       s.last_run_at },
         { "last_status",       s.last_status },
         { "last_job_id",       s.last_job_id ? json(s.last_job_id) : json(nullptr) },
         { "last_error",        s.last_error },
         { "created_at",        s.created_at },
         { "updated_at",        s.updated_at },
      };
   }

   bool FromJson(const json& j, ScheduleEntry* s, std::string* err)
   {
      try
      {
         s->name = j.value("name", "");
         if(s->name.empty()) { *err = "name is required"; return false; }
         if(!j.contains("ready_made_id") || !j["ready_made_id"].is_number_integer())
            { *err = "ready_made_id required"; return false; }
         s->ready_made_id    = j["ready_made_id"].get<int64_t>();
         s->frequency        = j.value("frequency",        std::string("daily"));
         s->time_hour        = j.value("time_hour",        8);
         s->time_minute      = j.value("time_minute",      0);
         s->day_of_week      = j.value("day_of_week",      1);
         s->day_of_month     = j.value("day_of_month",     1);
         s->every_n_hours    = j.value("every_n_hours",    1);
         s->telegram_chat_id = j.value("telegram_chat_id", std::string());
         s->delivery_format  = j.value("delivery_format",  std::string("csv"));
         if(s->delivery_format != "csv" && s->delivery_format != "text")
            { *err = "delivery_format must be 'csv' or 'text'"; return false; }
         s->enabled          = j.value("enabled",          true);
         s->folder_id = (j.contains("folder_id") && j["folder_id"].is_number_integer())
            ? j["folder_id"].get<int64_t>() : 0;
         //--- Helper: parse, clamp, dedupe an int array from JSON.
         auto readIntList = [&](const char* key, int lo, int hi) -> std::vector<int> {
            std::vector<int> out;
            if(!j.contains(key) || !j[key].is_array()) return out;
            std::set<int> seen;
            for(const auto& v : j[key])
            {
               if(!v.is_number_integer()) continue;
               int x = v.get<int>();
               if(x < lo || x > hi) continue;
               if(seen.insert(x).second) out.push_back(x);
            }
            std::sort(out.begin(), out.end());
            return out;
         };
         s->hours        = readIntList("hours",        0, 23);
         s->days_of_week = readIntList("days_of_week", 0,  6);
      }
      catch(const std::exception& e) { *err = e.what(); return false; }
      return true;
   }
}

void ScheduleRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/schedules", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = ScheduleRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& s : rows) out.push_back(ToJson(s));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/schedules/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto s = ScheduleRepo::Get(*ctx->db, id);
      if(!s) { SendError(res, 404, "schedule not found"); return; }
      res.set_content(ToJson(*s).dump(), "application/json");
   });

   srv.Post("/api/schedules", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      ScheduleEntry s; std::string err;
      if(!FromJson(j, &s, &err)) { SendError(res, 400, err); return; }
      s.next_run_at = Scheduler::ComputeNext(s, (int64_t)time(nullptr));
      ScheduleRepo::Insert(*ctx->db, s);
      res.set_content(ToJson(s).dump(), "application/json");
   });

   srv.Patch(R"(/api/schedules/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = ScheduleRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "schedule not found"); return; }
      ScheduleEntry s = *cur;
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string err;
      if(!FromJson(j, &s, &err)) { SendError(res, 400, err); return; }
      s.id = id;
      s.next_run_at = Scheduler::ComputeNext(s, (int64_t)time(nullptr));
      ScheduleRepo::Update(*ctx->db, s);
      res.set_content(ToJson(s).dump(), "application/json");
   });

   srv.Delete(R"(/api/schedules/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      ScheduleRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   //--- Force the next firing to be "now" so the scheduler dispatches on its
   //--- next tick (≤60s). Useful for manual testing of scheduled delivery.
   srv.Post(R"(/api/schedules/(\d+)/run-now)", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = ScheduleRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "schedule not found"); return; }
      ScheduleEntry s = *cur;
      s.next_run_at = (int64_t)time(nullptr);
      s.last_status = "";   // clear any previous dispatched state to allow re-fire
      ScheduleRepo::Update(*ctx->db, s);
      res.set_content(json{{"queued_for", s.next_run_at}}.dump(), "application/json");
   });
}
