//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       ReadyMadeRoutes.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "ReadyMadeRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../core/Scheduler.h"
#include "JobRunner.h"

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   json ToJson(const ReadyMadeReport& r)
   {
      return json{
         { "id",                r.id },
         { "name",              r.name },
         { "description",       r.description },
         { "template_id",       r.template_id },
         { "account_filter_id", r.account_filter_id ? json(r.account_filter_id) : json(nullptr) },
         { "date_strategy",     r.date_strategy },
         { "fixed_dates",       json::parse(r.fixed_dates_json, nullptr, false).is_object()
                                    ? json::parse(r.fixed_dates_json)
                                    : json::object() },
         { "relative_preset",   r.relative_preset },
         { "relative_n",        r.relative_n },
         { "top_n_override",    r.top_n_override },
         { "created_at",        r.created_at },
         { "updated_at",        r.updated_at },
      };
   }

   bool FromJson(const json& j, ReadyMadeReport* r, std::string* err)
   {
      try
      {
         r->name        = j.value("name", "");
         r->description = j.value("description", "");
         if(r->name.empty()) { *err = "name is required"; return false; }
         if(!j.contains("template_id") || !j["template_id"].is_number_integer())
            { *err = "template_id required"; return false; }
         r->template_id = j["template_id"].get<int64_t>();
         r->account_filter_id = j.contains("account_filter_id") && !j["account_filter_id"].is_null()
                                ? j["account_filter_id"].get<int64_t>() : 0;
         r->date_strategy = j.value("date_strategy", std::string("relative"));
         if(r->date_strategy != "fixed" && r->date_strategy != "relative")
            { *err = "date_strategy must be 'fixed' or 'relative'"; return false; }
         if(j.contains("fixed_dates") && j["fixed_dates"].is_object())
            r->fixed_dates_json = j["fixed_dates"].dump();
         else
            r->fixed_dates_json = "{}";
         r->relative_preset = j.value("relative_preset", std::string("last_n_days"));
         r->relative_n      = j.value("relative_n",      7);
         r->top_n_override  = j.value("top_n_override",  0u);
      }
      catch(const std::exception& e) { *err = e.what(); return false; }
      return true;
   }
}

void ReadyMadeRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/ready-made", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = ReadyMadeRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& r : rows) out.push_back(ToJson(r));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/ready-made/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto r = ReadyMadeRepo::Get(*ctx->db, id);
      if(!r) { SendError(res, 404, "ready-made not found"); return; }
      res.set_content(ToJson(*r).dump(), "application/json");
   });

   srv.Post("/api/ready-made", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      ReadyMadeReport r; std::string err;
      if(!FromJson(j, &r, &err)) { SendError(res, 400, err); return; }
      ReadyMadeRepo::Insert(*ctx->db, r);
      res.set_content(ToJson(r).dump(), "application/json");
   });

   srv.Patch(R"(/api/ready-made/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = ReadyMadeRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "ready-made not found"); return; }
      ReadyMadeReport r = *cur;
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string err;
      if(!FromJson(j, &r, &err)) { SendError(res, 400, err); return; }
      r.id = id;
      ReadyMadeRepo::Update(*ctx->db, r);
      res.set_content(ToJson(r).dump(), "application/json");
   });

   srv.Delete(R"(/api/ready-made/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      try { ReadyMadeRepo::Delete(*ctx->db, id); }
      catch(const std::exception& e) { SendError(res, 409, e.what()); return; }
      res.set_content(R"({"deleted":true})", "application/json");
   });

   //--- Run a ready-made now; body may override dates, account_filter_id, top_n,
   //--- manager_id (manager_id required either via override or via the bound
   //--- account-filter's manager).
   srv.Post(R"(/api/ready-made/(\d+)/run)", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto rm = ReadyMadeRepo::Get(*ctx->db, id);
      if(!rm) { SendError(res, 404, "ready-made not found"); return; }
      auto tpl = TemplateRepo::Get(*ctx->db, rm->template_id);
      if(!tpl) { SendError(res, 404, "template missing for this ready-made"); return; }

      json over = json::parse(req.body, nullptr, false);
      if(over.is_discarded()) over = json::object();

      const int64_t now = (int64_t)time(nullptr);
      json params;
      try { params = Scheduler::BuildRunParams(*rm, tpl->date_params, now); }
      catch(const std::exception& e) { SendError(res, 400, e.what()); return; }

      //--- Apply per-run overrides.
      if(over.contains("dates") && over["dates"].is_object())
         for(auto it = over["dates"].begin(); it != over["dates"].end(); ++it)
            if(it.value().is_string()) params["dates"][it.key()] = it.value().get<std::string>();
      if(over.contains("account_filter_id"))
         params["account_filter_id"] = over["account_filter_id"];
      if(over.contains("top_n"))
         params["top_n"] = over["top_n"];

      //--- Resolve manager_id: override > account_filter.manager_id
      int64_t manager_id = 0;
      if(over.contains("manager_id") && over["manager_id"].is_number_integer())
         manager_id = over["manager_id"].get<int64_t>();
      else
      {
         const int64_t af_id = params.value("account_filter_id", (int64_t)0);
         if(af_id)
         {
            if(auto af = AccountFilterRepo::Get(*ctx->db, af_id))
               manager_id = af->manager_id;
         }
      }
      if(!manager_id)
      {
         SendError(res, 400, "manager_id not resolvable — pass override or bind a manager-scoped account filter");
         return;
      }
      params["manager_id"] = manager_id;

      JobRow row;
      row.template_id       = rm->template_id;
      row.manager_id        = manager_id;
      row.account_filter_id = params.value("account_filter_id", (int64_t)0);
      row.params_json       = params.dump();
      JobRepo::Create(*ctx->db, row);
      ctx->jobs->Enqueue(row.id);

      res.set_content(json{{"job_id", row.id}, {"status", "queued"}}.dump(), "application/json");
   });
}
