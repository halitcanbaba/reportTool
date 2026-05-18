//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       BlueprintRoutes.cpp        |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "BlueprintRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include "../reports/Expression.h"
#include "../reports/FieldCatalog.h"

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   json ToJson(const FormulaBlueprint& b)
   {
      json dps = json::array();
      for(const auto& d : b.date_params) dps.push_back(d);
      return json{
         { "id",          b.id },
         { "name",        b.name },
         { "description", b.description },
         { "date_params", dps },
         { "expr",        b.expr ? Expression::NodeToJson(*b.expr) : json(nullptr) },
         { "folder_id",   b.folder_id ? json(b.folder_id) : json(nullptr) },
         { "created_at",  b.created_at },
         { "updated_at",  b.updated_at },
      };
   }

   bool FromJson(const json& j, FormulaBlueprint* b, std::string* err)
   {
      try
      {
         b->name        = j.value("name", "");
         b->description = j.value("description", "");
         if(b->name.empty()) { *err = "name is required"; return false; }

         b->date_params.clear();
         if(j.contains("date_params") && j["date_params"].is_array())
            for(const auto& v : j["date_params"]) if(v.is_string()) b->date_params.push_back(v.get<std::string>());

         if(!j.contains("expr") || j["expr"].is_null())
         { *err = "expr is required"; return false; }
         if(!Expression::NodeFromJson(j["expr"], &b->expr, err)) return false;
         //--- PATCH semantics: missing key leaves the existing folder_id intact.
         if(j.contains("folder_id"))
         {
            if(j["folder_id"].is_null())                  b->folder_id = 0;
            else if(j["folder_id"].is_number_integer())   b->folder_id = j["folder_id"].get<int64_t>();
         }
      }
      catch(const std::exception& e) { *err = e.what(); return false; }
      return true;
   }

   //--- Wrap the blueprint into a single-column ReportTemplate so we can
   //--- reuse FieldCatalog::Validate without duplicating logic.
   std::vector<FieldCatalog::ValidationError> ValidateBlueprint(const FormulaBlueprint& b)
   {
      ReportTemplate tpl;
      tpl.name = "<blueprint>";
      tpl.row_model = "per_account";
      tpl.date_params = b.date_params;
      ColumnSpec col;
      col.key = "expr";
      col.label = "expr";
      col.kind = ColumnSpec::Kind::Formula;
      col.format = ColumnSpec::Format::Money;
      col.expr = b.expr;
      tpl.columns.push_back(col);
      tpl.sort.column_key = "expr";
      tpl.sort.descending = true;
      return FieldCatalog::Validate(tpl);
   }
}

void BlueprintRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   srv.Get("/api/blueprints", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = BlueprintRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& r : rows) out.push_back(ToJson(r));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/blueprints/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto b = BlueprintRepo::Get(*ctx->db, id);
      if(!b) { SendError(res, 404, "blueprint not found"); return; }
      res.set_content(ToJson(*b).dump(), "application/json");
   });

   srv.Post("/api/blueprints", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      FormulaBlueprint b; std::string err;
      if(!FromJson(j, &b, &err)) { SendError(res, 400, err); return; }

      auto errs = ValidateBlueprint(b);
      if(!errs.empty())
      {
         json arr = json::array();
         for(const auto& e : errs) arr.push_back({ {"path", e.path}, {"message", e.message} });
         res.status = 400;
         res.set_content(json{ {"error", "validation failed"}, {"errors", arr} }.dump(),
                         "application/json");
         return;
      }

      try { BlueprintRepo::Insert(*ctx->db, b); }
      catch(const std::exception& e) { SendError(res, 500, e.what()); return; }
      res.set_content(ToJson(b).dump(), "application/json");
   });

   srv.Patch(R"(/api/blueprints/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = BlueprintRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "blueprint not found"); return; }
      FormulaBlueprint b = *cur;
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string err;
      if(!FromJson(j, &b, &err)) { SendError(res, 400, err); return; }
      b.id = id;

      auto errs = ValidateBlueprint(b);
      if(!errs.empty())
      {
         json arr = json::array();
         for(const auto& e : errs) arr.push_back({ {"path", e.path}, {"message", e.message} });
         res.status = 400;
         res.set_content(json{ {"error", "validation failed"}, {"errors", arr} }.dump(),
                         "application/json");
         return;
      }

      BlueprintRepo::Update(*ctx->db, b);
      res.set_content(ToJson(b).dump(), "application/json");
   });

   srv.Delete(R"(/api/blueprints/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      BlueprintRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });
}
