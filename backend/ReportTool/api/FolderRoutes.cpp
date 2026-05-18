//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       FolderRoutes.cpp            |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "FolderRoutes.h"
#include "../third_party/httplib.h"
#include "../db/Repos.h"
#include <algorithm>

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
         { "parent_id",   f.parent_id ? json(f.parent_id) : json(nullptr) },
         { "item_count",  f.item_count },
         { "created_at",  f.created_at },
         { "updated_at",  f.updated_at },
      };
   }

   //--- Walk parent chain from `start` toward the root; returns true if `needle`
   //--- is encountered (i.e. nesting `needle` under `start` would create a cycle).
   bool IsAncestor(SqliteDb& db, int64_t needle, int64_t start)
   {
      int64_t cur = start;
      for(int hops = 0; hops < 64 && cur != 0; ++hops)
      {
         if(cur == needle) return true;
         auto f = FolderRepo::Get(db, cur);
         if(!f) return false;
         cur = f->parent_id;
      }
      return false;
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

   //--- POST /api/folders  { entity_type, name, parent_id? } → 201ish with the row.
   srv.Post("/api/folders", [ctx](const httplib::Request& req, httplib::Response& res){
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.is_object()) { SendError(res, 400, "invalid json"); return; }
      FolderRow f;
      f.entity_type = j.value("entity_type", "");
      f.name        = j.value("name", "");
      f.sort_order  = j.value("sort_order", 0);
      if(!IsValidEntityType(f.entity_type)) { SendError(res, 400, "invalid entity_type"); return; }
      if(f.name.empty())                    { SendError(res, 400, "name required"); return; }
      if(j.contains("parent_id") && !j["parent_id"].is_null())
      {
         if(!j["parent_id"].is_number_integer())
            { SendError(res, 400, "parent_id must be integer or null"); return; }
         const int64_t pid = j["parent_id"].get<int64_t>();
         auto parent = FolderRepo::Get(*ctx->db, pid);
         if(!parent || parent->entity_type != f.entity_type)
            { SendError(res, 400, "parent_id refers to a folder of a different entity_type"); return; }
         f.parent_id = pid;
      }
      FolderRepo::Insert(*ctx->db, f);
      res.set_content(FolderToJson(f).dump(), "application/json");
   });

   //--- PATCH /api/folders/:id  { name?, sort_order?, parent_id? } → updated row.
   //--- parent_id supports PATCH semantics: omitted leaves it; explicit null
   //--- promotes to top level; integer re-parents (must match entity_type,
   //--- must not create a cycle).
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
      if(j.contains("parent_id"))
      {
         if(j["parent_id"].is_null()) next.parent_id = 0;
         else if(j["parent_id"].is_number_integer())
         {
            const int64_t pid = j["parent_id"].get<int64_t>();
            if(pid == id) { SendError(res, 400, "folder cannot be its own parent"); return; }
            auto parent = FolderRepo::Get(*ctx->db, pid);
            if(!parent || parent->entity_type != cur->entity_type)
               { SendError(res, 400, "parent_id refers to a folder of a different entity_type"); return; }
            if(IsAncestor(*ctx->db, id, pid))
               { SendError(res, 400, "cycle: parent_id is a descendant of this folder"); return; }
            next.parent_id = pid;
         }
         else { SendError(res, 400, "parent_id must be integer or null"); return; }
      }
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

   //--- PATCH /api/folders/:id/move  { parent_id: number|null, before_id?: number|null }
   //--- Drag-drop reposition: nests this folder under `parent_id` (null = root)
   //--- and places it immediately before `before_id` among its new siblings,
   //--- or at the end when `before_id` is null/missing. Sibling sort_order is
   //--- renumbered to a clean 10, 20, 30 … sequence so subsequent reorders
   //--- always have headroom.
   srv.Patch(R"(/api/folders/(\d+)/move)",
             [ctx](const httplib::Request& req, httplib::Response& res){
      const int64_t id = std::stoll(req.matches[1]);
      auto target = FolderRepo::Get(*ctx->db, id);
      if(!target) { SendError(res, 404, "folder not found"); return; }
      json j = json::parse(req.body, nullptr, false);
      if(j.is_discarded() || !j.is_object()) { SendError(res, 400, "invalid json"); return; }

      //--- Resolve new parent_id with cycle + entity_type validation.
      int64_t new_parent = 0;
      if(j.contains("parent_id") && !j["parent_id"].is_null())
      {
         if(!j["parent_id"].is_number_integer())
            { SendError(res, 400, "parent_id must be integer or null"); return; }
         new_parent = j["parent_id"].get<int64_t>();
         if(new_parent == id) { SendError(res, 400, "folder cannot be its own parent"); return; }
         auto parent = FolderRepo::Get(*ctx->db, new_parent);
         if(!parent || parent->entity_type != target->entity_type)
            { SendError(res, 400, "parent_id refers to a folder of a different entity_type"); return; }
         if(IsAncestor(*ctx->db, id, new_parent))
            { SendError(res, 400, "cycle: parent_id is a descendant of this folder"); return; }
      }

      //--- Resolve before_id (sibling under whose row the target is inserted).
      int64_t before_id = 0;
      if(j.contains("before_id") && !j["before_id"].is_null())
      {
         if(!j["before_id"].is_number_integer())
            { SendError(res, 400, "before_id must be integer or null"); return; }
         before_id = j["before_id"].get<int64_t>();
         if(before_id == id) { SendError(res, 400, "before_id cannot equal folder id"); return; }
         auto bf = FolderRepo::Get(*ctx->db, before_id);
         if(!bf || bf->entity_type != target->entity_type || bf->parent_id != new_parent)
            { SendError(res, 400, "before_id is not a sibling under parent_id"); return; }
      }

      //--- Pull the new-parent's sibling list (excluding the target), insert
      //--- the target at the requested position, then renumber 10, 20, 30 …
      auto all = FolderRepo::ListByEntity(*ctx->db, target->entity_type);
      std::vector<FolderRow> siblings;
      for(const auto& f : all)
         if(f.parent_id == new_parent && f.id != id)
            siblings.push_back(f);
      //--- ListByEntity already returns sort_order, id ordering.
      size_t insert_at = siblings.size();   // append by default
      if(before_id)
      {
         for(size_t i = 0; i < siblings.size(); ++i)
            if(siblings[i].id == before_id) { insert_at = i; break; }
      }
      FolderRow moved = *target;
      moved.parent_id = new_parent;
      siblings.insert(siblings.begin() + insert_at, moved);

      //--- Renumber + persist. The target gets its new parent_id + sort_order,
      //--- siblings keep their parent_id and only update sort_order when changed.
      for(size_t i = 0; i < siblings.size(); ++i)
      {
         FolderRow& s = siblings[i];
         const int desired = (int)((i + 1) * 10);
         if(s.id == id)
         {
            s.sort_order = desired;
            FolderRepo::Update(*ctx->db, s);
            moved = s;
         }
         else if(s.sort_order != desired)
         {
            s.sort_order = desired;
            FolderRepo::Update(*ctx->db, s);
         }
      }
      res.set_content(FolderToJson(moved).dump(), "application/json");
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
