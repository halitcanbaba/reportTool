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
#include <unordered_map>

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   //--- Resolve template names for a job list (one extra lookup per unique template).
   std::unordered_map<int64_t, std::string>
   FetchTemplateNames(AppContext* ctx, const std::vector<JobRow>& jobs)
   {
      std::unordered_map<int64_t, std::string> names;
      for(const auto& j : jobs) names[j.template_id] = "";
      for(auto& kv : names)
      {
         auto t = TemplateRepo::Get(*ctx->db, kv.first);
         if(t) kv.second = t->name;
      }
      return names;
   }

   json JobToJson(const JobRow& j, const std::string& template_name)
   {
      json out = {
         { "id",                j.id },
         { "manager_id",        j.manager_id },
         { "template_id",       j.template_id },
         { "account_filter_id", j.account_filter_id ? json(j.account_filter_id) : json(nullptr) },
         { "template_name",     template_name },
         { "params_json",       j.params_json },
         { "status",            JobStatusName(j.status) },
         { "progress",          j.progress },
         { "error_message",     j.error_message },
         { "created_at",        j.created_at },
         { "started_at",        j.started_at },
         { "completed_at",      j.completed_at },
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
   //--- Run a template ----------------------------------------------
   srv.Post("/api/reports/run", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      if(!j.contains("template_id") || !j["template_id"].is_number_integer())
         { SendError(res, 400, "template_id required"); return; }
      if(!j.contains("manager_id") || !j["manager_id"].is_number_integer())
         { SendError(res, 400, "manager_id required"); return; }

      JobRow row;
      row.template_id      = j["template_id"].get<int64_t>();
      row.manager_id       = j["manager_id"].get<int64_t>();
      row.account_filter_id= j.contains("account_filter_id") && j["account_filter_id"].is_number_integer()
                              ? j["account_filter_id"].get<int64_t>() : 0;
      row.params_json      = j.dump();

      //--- sanity: template exists?
      auto t = TemplateRepo::Get(*ctx->db, row.template_id);
      if(!t) { SendError(res, 404, "template not found"); return; }
      auto m = ManagerRepo::Get(*ctx->db, row.manager_id);
      if(!m) { SendError(res, 404, "manager not found"); return; }

      JobRepo::Create(*ctx->db, row);
      ctx->jobs->Enqueue(row.id);
      res.set_content(json{ {"job_id", row.id}, {"status", "queued"} }.dump(), "application/json");
   });

   //--- List / get / delete jobs ------------------------------------
   srv.Get("/api/reports/jobs", [ctx](const httplib::Request& req, httplib::Response& res){
      int limit = 50;
      if(req.has_param("limit"))
         limit = std::max(1, std::min(500, std::stoi(req.get_param_value("limit"))));
      auto rows = JobRepo::List(*ctx->db, limit);
      auto names = FetchTemplateNames(ctx, rows);
      json out = json::array();
      for(const auto& j : rows) out.push_back(JobToJson(j, names[j.template_id]));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/reports/jobs/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto j = JobRepo::Get(*ctx->db, id);
      if(!j) { SendError(res, 404, "job not found"); return; }
      auto t = TemplateRepo::Get(*ctx->db, j->template_id);
      const std::string name = t ? t->name : std::string();
      res.set_content(JobToJson(*j, name).dump(), "application/json");
   });

   srv.Delete(R"(/api/reports/jobs/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      JobRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   //--- File downloads ---------------------------------------------
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
