//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       TemplateRoutes.cpp         |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "TemplateRoutes.h"
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

   //--- Template <-> JSON --------------------------------------------

   const char* KindStr(ColumnSpec::Kind k){ return k == ColumnSpec::Kind::Identifier ? "identifier" : "formula"; }
   const char* FmtStr(ColumnSpec::Format f)
   {
      switch(f)
      {
         case ColumnSpec::Format::Money:  return "money";
         case ColumnSpec::Format::Pct:    return "pct";
         case ColumnSpec::Format::Int:    return "int";
         case ColumnSpec::Format::Text:   return "text";
         case ColumnSpec::Format::Date:   return "date";
         case ColumnSpec::Format::Number: return "number";
      }
      return "money";
   }
   ColumnSpec::Kind   KindFrom(const std::string& s){ return s == "identifier" ? ColumnSpec::Kind::Identifier : ColumnSpec::Kind::Formula; }
   ColumnSpec::Format FmtFrom (const std::string& s)
   {
      if(s == "pct")    return ColumnSpec::Format::Pct;
      if(s == "int")    return ColumnSpec::Format::Int;
      if(s == "text")   return ColumnSpec::Format::Text;
      if(s == "date")   return ColumnSpec::Format::Date;
      if(s == "number") return ColumnSpec::Format::Number;
      return ColumnSpec::Format::Money;
   }

   json ColumnsToJson(const std::vector<ColumnSpec>& cols)
   {
      json a = json::array();
      for(const auto& c : cols)
      {
         json j;
         j["key"]    = c.key;
         j["label"]  = c.label;
         j["kind"]   = KindStr(c.kind);
         j["format"] = FmtStr(c.format);
         if(c.kind == ColumnSpec::Kind::Identifier)
         {
            j["source"]    = c.source;
            j["pivot_key"] = c.pivot_key;
         }
         else if(c.expr) j["expr"] = Expression::NodeToJson(*c.expr);
         if(c.row_predicate)
            j["row_predicate"] = Expression::PredicateToJson(*c.row_predicate);
         a.push_back(std::move(j));
      }
      return a;
   }

   bool ColumnsFromJson(const json& a, std::vector<ColumnSpec>* out, std::string* err)
   {
      if(!a.is_array()) { *err = "columns must be a JSON array"; return false; }
      out->clear();
      bool any_explicit_pivot = false;
      for(const auto& j : a)
      {
         ColumnSpec c;
         c.key    = j.value("key", "");
         c.label  = j.value("label", "");
         c.kind   = KindFrom(j.value("kind", "formula"));
         c.format = FmtFrom(j.value("format", "money"));
         if(c.kind == ColumnSpec::Kind::Identifier)
         {
            c.source = j.value("source", "");
            if(j.contains("pivot_key") && j["pivot_key"].is_boolean())
            {
               c.pivot_key = j["pivot_key"].get<bool>();
               any_explicit_pivot = any_explicit_pivot || c.pivot_key;
            }
         }
         else if(j.contains("expr"))
         {
            std::string e;
            if(!Expression::NodeFromJson(j["expr"], &c.expr, &e))
            {
               *err = "column '" + c.key + "': " + e;
               return false;
            }
         }
         if(j.contains("row_predicate") && !j["row_predicate"].is_null())
         {
            std::string e;
            if(!Expression::PredicateFromJson(j["row_predicate"], &c.row_predicate, &e))
            {
               *err = "column '" + c.key + "' row_predicate: " + e;
               return false;
            }
         }
         out->push_back(std::move(c));
      }
      //--- Backward-compat: legacy templates carry no pivot_key. Mark the first
      //--- identifier as the implicit pivot so single-pivot reports keep
      //--- working without the user re-editing.
      if(!any_explicit_pivot)
      {
         for(auto& c : *out)
            if(c.kind == ColumnSpec::Kind::Identifier) { c.pivot_key = true; break; }
      }
      return true;
   }

   json TemplateToJson(const ReportTemplate& t)
   {
      json dps = json::array();
      for(const auto& d : t.date_params) dps.push_back(d);
      return json{
         { "id",            t.id },
         { "name",          t.name },
         { "description",   t.description },
         { "row_model",     t.row_model },
         { "date_params",   dps },
         { "columns",       ColumnsToJson(t.columns) },
         { "sort",          json{
                                {"column_key", t.sort.column_key},
                                {"direction",  t.sort.descending ? "desc" : "asc"},
                                {"abs",        t.sort.abs_value},
                             } },
         { "default_top_n", t.default_top_n },
         { "folder_id",     t.folder_id ? json(t.folder_id) : json(nullptr) },
         { "sort_order",    t.sort_order },
         { "deleted_at",    t.deleted_at ? json(t.deleted_at) : json(nullptr) },
         { "created_at",    t.created_at },
         { "updated_at",    t.updated_at },
      };
   }

   bool TemplateFromJson(const json& j, ReportTemplate* t, std::string* err)
   {
      try
      {
         t->name        = j.value("name", "");
         if(t->name.empty()) { *err = "name is required"; return false; }
         t->description = j.value("description", "");
         t->row_model   = j.value("row_model", "per_account");
         t->date_params.clear();
         if(j.contains("date_params") && j["date_params"].is_array())
            for(const auto& v : j["date_params"]) if(v.is_string()) t->date_params.push_back(v.get<std::string>());
         if(j.contains("columns"))
         {
            if(!ColumnsFromJson(j["columns"], &t->columns, err)) return false;
         }
         if(j.contains("sort") && j["sort"].is_object())
         {
            t->sort.column_key = j["sort"].value("column_key", "");
            t->sort.descending = j["sort"].value("direction", std::string("desc")) != "asc";
            //--- Backward compat: absent `abs` defaults to false.
            t->sort.abs_value  = j["sort"].value("abs", false);
         }
         t->default_top_n = j.value("default_top_n", 0u);
         //--- PATCH semantics: missing key leaves the existing folder_id intact;
         //--- explicit null clears, integer sets. For POST, *t defaults to 0.
         if(j.contains("folder_id"))
         {
            if(j["folder_id"].is_null())                  t->folder_id = 0;
            else if(j["folder_id"].is_number_integer())   t->folder_id = j["folder_id"].get<int64_t>();
         }
      }
      catch(const std::exception& e) { *err = e.what(); return false; }
      return true;
   }

   json ValidateErrorsToJson(const std::vector<FieldCatalog::ValidationError>& errs)
   {
      json a = json::array();
      for(const auto& e : errs) a.push_back({ {"path", e.path}, {"message", e.message} });
      return a;
   }
}

void TemplateRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   //--- Field catalog (single source of truth for designer + validator) ---
   srv.Get("/api/reports/fields", [](const httplib::Request&, httplib::Response& res){
      res.set_content(FieldCatalog::CatalogToJson().dump(), "application/json");
   });

   //--- Template validate (no persistence) ---
   srv.Post("/api/templates/validate", [](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      ReportTemplate t; std::string err;
      if(!TemplateFromJson(j, &t, &err))
      {
         json out = { {"ok", false}, {"errors", json::array({ {{"path","root"},{"message", err}} })} };
         res.status = 400; res.set_content(out.dump(), "application/json"); return;
      }
      const auto errs = FieldCatalog::Validate(t);
      json out = { {"ok", errs.empty()}, {"errors", ValidateErrorsToJson(errs)} };
      res.set_content(out.dump(), "application/json");
   });

   //--- CRUD ----------------------------------------------------------
   srv.Get("/api/templates", [ctx](const httplib::Request&, httplib::Response& res){
      auto rows = TemplateRepo::ListAll(*ctx->db);
      json out = json::array();
      for(const auto& r : rows) out.push_back(TemplateToJson(r));
      res.set_content(out.dump(), "application/json");
   });

   srv.Get(R"(/api/templates/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto t = TemplateRepo::Get(*ctx->db, id);
      if(!t) { SendError(res, 404, "template not found"); return; }
      res.set_content(TemplateToJson(*t).dump(), "application/json");
   });

   srv.Post("/api/templates", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      ReportTemplate t; std::string err;
      if(!TemplateFromJson(j, &t, &err)) { SendError(res, 400, err); return; }
      const auto errs = FieldCatalog::Validate(t);
      if(!errs.empty())
      {
         res.status = 400;
         res.set_content(json{ {"error","validation failed"}, {"errors", ValidateErrorsToJson(errs)} }.dump(),
                         "application/json");
         return;
      }
      TemplateRepo::Insert(*ctx->db, t);
      res.set_content(TemplateToJson(t).dump(), "application/json");
   });

   srv.Patch(R"(/api/templates/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = TemplateRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "template not found"); return; }
      ReportTemplate t = *cur;
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded()) { SendError(res, 400, "invalid json"); return; }
      std::string err;
      if(!TemplateFromJson(j, &t, &err)) { SendError(res, 400, err); return; }
      t.id = id;
      const auto errs = FieldCatalog::Validate(t);
      if(!errs.empty())
      {
         res.status = 400;
         res.set_content(json{ {"error","validation failed"}, {"errors", ValidateErrorsToJson(errs)} }.dump(),
                         "application/json");
         return;
      }
      TemplateRepo::Update(*ctx->db, t);
      res.set_content(TemplateToJson(t).dump(), "application/json");
   });

   srv.Delete(R"(/api/templates/(\d+))", [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      try
      {
         TemplateRepo::Delete(*ctx->db, id);
         res.set_content(R"({"deleted":true})", "application/json");
      }
      catch(const std::exception& e)
      {
         SendError(res, 409, std::string("delete failed: ") + e.what());
      }
   });
}
