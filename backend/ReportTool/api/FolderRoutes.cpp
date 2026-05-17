//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       FolderRoutes.cpp            |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "FolderRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"

using nlohmann::json;

namespace
{
   void SendError(httplib::Response& res, int status, const std::string& msg)
   {
      res.status = status;
      res.set_content(json{ {"error", msg} }.dump(), "application/json");
   }

   bool IsValidEntityType(const std::string& t)
   {
      return t == "template"   || t == "schedule"  || t == "blueprint"
          || t == "ready_made" || t == "account_filter";
   }

   json FolderToJson(const FolderRow& f)
   {
      return json{
         { "id",          f.id },
         { "entity_type", f.entity_type },
         { "name",        f.name },
         { "sort_order",  f.sort_order },
         { "item_count",  f.item_count },
         { "created_at",  f.created_at },
         { "updated_at",  f.updated_at },
      };
   }
}

void FolderRoutes::Register(httplib::Server& srv, AppContext* ctx)
{
   //--- GET /api/folders?entity_type=…  → list with counts.
   srv.Get("/api/folders", [ctx](const httplib::Request& req, httplib::Response& res){
      const std::string et = req.get_param_value("entity_type");
      if(!IsValidEntityType(et))
      {
         SendError(res, 400, "entity_type required (template|schedule|blueprint|ready_made|account_filter)");
         return;
      }
      auto rows = FolderRepo::ListByEntity(*ctx->db, et);
      json out = json::array();
      for(const auto& f : rows) out.push_back(FolderToJson(f));
      res.set_content(out.dump(), "application/json");
   });

   //--- POST /api/folders  { entity_type, name } → 201ish with the row.
   srv.Post("/api/folders", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.is_object()) { SendError(res, 400, "invalid json"); return; }
      FolderRow f;
      f.entity_type = j.value("entity_type", "");
      f.name        = j.value("name", "");
      f.sort_order  = j.value("sort_order", 0);
      if(!IsValidEntityType(f.entity_type)) { SendError(res, 400, "invalid entity_type"); return; }
      if(f.name.empty())                    { SendError(res, 400, "name required"); return; }
      FolderRepo::Insert(*ctx->db, f);
      res.set_content(FolderToJson(f).dump(), "application/json");
   });

   //--- PATCH /api/folders/:id  { name?, sort_order? } → updated row.
   srv.Patch(R"(/api/folders/(\d+))",
             [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto cur = FolderRepo::Get(*ctx->db, id);
      if(!cur) { SendError(res, 404, "folder not found"); return; }
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.is_object()) { SendError(res, 400, "invalid json"); return; }
      FolderRow next = *cur;
      if(j.contains("name") && j["name"].is_string()) next.name = j["name"].get<std::string>();
      if(j.contains("sort_order") && j["sort_order"].is_number_integer())
         next.sort_order = j["sort_order"].get<int>();
      if(next.name.empty()) { SendError(res, 400, "name cannot be empty"); return; }
      FolderRepo::Update(*ctx->db, next);
      res.set_content(FolderToJson(next).dump(), "application/json");
   });

   //--- DELETE /api/folders/:id  — children fall to Unfiled via SET NULL.
   srv.Delete(R"(/api/folders/(\d+))",
              [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      FolderRepo::Delete(*ctx->db, id);
      res.set_content(R"({"deleted":true})", "application/json");
   });

   //--- PATCH /api/folders/move  { entity_type, entity_id, folder_id|null }
   //--- Single endpoint used by drag-drop on the list pages.
   srv.Patch("/api/folders/move",
             [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.is_object()) { SendError(res, 400, "invalid json"); return; }
      const std::string et = j.value("entity_type", "");
      if(!IsValidEntityType(et)) { SendError(res, 400, "invalid entity_type"); return; }
      if(!j.contains("entity_id") || !j["entity_id"].is_number_integer())
         { SendError(res, 400, "entity_id required"); return; }
      const int64_t entity_id = j["entity_id"].get<int64_t>();
      int64_t folder_id = 0;
      if(j.contains("folder_id") && !j["folder_id"].is_null())
      {
         if(!j["folder_id"].is_number_integer())
            { SendError(res, 400, "folder_id must be integer or null"); return; }
         folder_id = j["folder_id"].get<int64_t>();
      }
      const bool ok = FolderRepo::Move(*ctx->db, et, entity_id, folder_id);
      if(!ok) { SendError(res, 404, "entity not found"); return; }
      res.set_content(R"({"ok":true})", "application/json");
   });
}
