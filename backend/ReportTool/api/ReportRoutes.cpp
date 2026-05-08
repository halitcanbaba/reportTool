//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       ReportRoutes.cpp           |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "ReportRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "JobRunner.h"
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      json j = { { "error", msg } };
      res.status = status; res.set_content(j.dump(), "application/json");
   }

   json JobToJson(const JobRow& j, AppContext* ctx)
   {
      json out = {
         { "id",            j.id },
         { "manager_id",    j.manager_id },
         { "kind",          ReportKindName(j.kind) },
         { "params_json",   j.params_json },
         { "status",        JobStatusName(j.status) },
         { "progress",      j.progress },
         { "error_message", j.error_message },
         { "created_at",    j.created_at },
         { "started_at",    j.started_at },
         { "completed_at",  j.completed_at },
      };
      if(!j.csv_filename.empty())
         out["csv_url"]  = std::string("/api/reports/jobs/") + std::to_string(j.id) + "/download.csv";
      if(!j.xlsx_filename.empty())
         out["xlsx_url"] = std::string("/api/reports/jobs/") + std::to_string(j.id) + "/download.xlsx";
      if(!j.summary_json.empty())
      {
         json p = json::parse(j.summary_json, nullptr, false);
         if(!p.is_discarded()) out["preview"] = p;
      }
      return out;
   }
}

void ReportRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Post("/api/reports/top-winner", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.contains("manager_id")) { SendError(res, 400, "manager_id required"); return; }
      JobRow row;
      row.manager_id  = j["manager_id"].get<int64_t>();
      row.kind        = ReportKind::TopWinner;
      row.params_json = j.dump();
      JobRepo::Create(*ctx->db, row);
      ctx->jobs->Enqueue(row.id);
      res.set_content(json{{"job_id", row.id}, {"status", "queued"}}.dump(), "application/json");
   });

   srv.Post("/api/reports/summary", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.contains("manager_id")) { SendError(res, 400, "manager_id required"); return; }
      JobRow row;
      row.manager_id  = j["manager_id"].get<int64_t>();
      row.kind        = ReportKind::Summary;
      row.params_json = j.dump();
      JobRepo::Create(*ctx->db, row);
      ctx->jobs->Enqueue(row.id);
      res.set_content(json{{"job_id", row.id}, {"status", "queued"}}.dump(), "application/json");
   });

   srv.Get("/api/reports/jobs", [ctx](const httplib::Request& req, httplib::Response& res){
      int limit = 50;
      if(req.has_param("limit")) limit = std::max(1, std::min(500, std::stoi(req.get_param_value("limit"))));
      auto rows = JobRepo::List(*ctx->db, limit);
      json out = json::array();
      for(const auto& j : rows) out.push_back(JobToJson(j, ctx));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/reports/jobs/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto j = JobRepo::Get(*ctx->db, id);
      if(!j) { SendError(res, 404, "job not found"); return; }
      res.set_content(JobToJson(*j, ctx).dump(), "application/json");
   });

   srv.Delete(R"(/api/reports/jobs/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      JobRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   auto stream_file = [](httplib::Response& res, const std::string& path,
                         const std::string& mime, const std::string& download_name) {
      std::ifstream f(path, std::ios::binary);
      if(!f) { res.status = 404; res.set_content(R"({"error":"file not found"})", "application/json"); return; }
      std::stringstream ss; ss << f.rdbuf();
      res.set_header("Content-Disposition", "attachment; filename=\"" + download_name + "\"");
      res.set_content(ss.str(), mime);
   };

   srv.Get(R"(/api/reports/jobs/(\d+)/download\.csv)", [ctx, stream_file](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto j = JobRepo::Get(*ctx->db, id);
      if(!j || j->csv_filename.empty()) { res.status = 404; return; }
      const std::string path = j->output_dir + "/" + j->csv_filename;
      stream_file(res, path, "text/csv; charset=utf-8", j->csv_filename);
   });

   srv.Get(R"(/api/reports/jobs/(\d+)/download\.xlsx)", [ctx, stream_file](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto j = JobRepo::Get(*ctx->db, id);
      if(!j || j->xlsx_filename.empty()) { res.status = 404; return; }
      const std::string path = j->output_dir + "/" + j->xlsx_filename;
      stream_file(res, path,
                  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
                  j->xlsx_filename);
   });
}
