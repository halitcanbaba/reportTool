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

   //--- A row in the unified folder + entity sibling list at one level. `kind`
   //--- is "folder" or "entity"; id resolves into the matching table.
   struct LevelItem
   {
      std::string kind;
      int64_t     id;
      int         sort_order;
   };

   //--- Pull every folder and every entity at the given level, merged + sorted
   //--- by (sort_order, id). Used by both folder-move and entity-move handlers
   //--- so a drag-drop reorder can place a folder between two entities (and
   //--- vice versa) — folders no longer get forced to the bottom.
   std::vector<LevelItem> FetchLevelSiblings(SqliteDb& db, const std::string& entity_type,
                                             int64_t target_parent_folder_id)
   {
      std::vector<LevelItem> out;
      //--- Folders at this level.
      for(const auto& f : FolderRepo::ListByEntity(db, entity_type))
         if(f.parent_id == target_parent_folder_id)
            out.push_back({ "folder", f.id, f.sort_order });
      //--- Entities at this level.
      for(const auto& e : FolderRepo::ListEntitiesAtLevel(db, entity_type, target_parent_folder_id))
         out.push_back({ "entity", e.id, e.sort_order });
      std::sort(out.begin(), out.end(), [](const LevelItem& a, const LevelItem& b){
         if(a.sort_order != b.sort_order) return a.sort_order < b.sort_order;
         return a.id < b.id;
      });
      return out;
   }

   //--- Renumber every sibling at the target level to (10, 20, 30, …) with the
   //--- moved item placed before `before` (or appended when before.id == 0).
   //--- Updates the moved item too: folder gets parent_id+sort_order; entity
   //--- gets folder_id+sort_order. Caller has already validated cycle / type.
   void ApplyMixedReorder(SqliteDb& db, const std::string& entity_type,
                          const std::string& moving_kind, int64_t moving_id,
                          int64_t target_parent_folder_id,
                          const std::string& before_kind, int64_t before_id,
                          int* moved_sort_order_out)
   {
      auto siblings = FetchLevelSiblings(db, entity_type, target_parent_folder_id);
      //--- Remove moved item if it's already in the list (re-position case).
      for(auto it = siblings.begin(); it != siblings.end(); )
      {
         if(it->kind == moving_kind && it->id == moving_id) it = siblings.erase(it);
         else                                                ++it;
      }
      //--- Find insertion index.
      size_t insert_at = siblings.size();
      if(before_id)
      {
         for(size_t i = 0; i < siblings.size(); ++i)
            if(siblings[i].kind == before_kind && siblings[i].id == before_id)
               { insert_at = i; break; }
      }
      LevelItem moved{ moving_kind, moving_id, 0 };
      siblings.insert(siblings.begin() + insert_at, moved);

      //--- Renumber 10, 20, 30, … and persist only the rows whose sort_order
      //--- (or parent) changes.
      for(size_t i = 0; i < siblings.size(); ++i)
      {
         LevelItem& s = siblings[i];
         const int desired = (int)((i + 1) * 10);
         const bool is_moved = (s.kind == moving_kind && s.id == moving_id);
         if(s.kind == "folder")
         {
            if(is_moved)
            {
               auto f = FolderRepo::Get(db, s.id);
               if(f)
               {
                  f->parent_id = target_parent_folder_id;
                  f->sort_order = desired;
                  FolderRepo::Update(db, *f);
                  if(moved_sort_order_out) *moved_sort_order_out = desired;
               }
            }
            else if(s.sort_order != desired)
            {
               auto f = FolderRepo::Get(db, s.id);
               if(f) { f->sort_order = desired; FolderRepo::Update(db, *f); }
            }
         }
         else // entity
         {
            if(is_moved)
            {
               FolderRepo::MoveEntityWithOrder(db, entity_type, s.id,
                                               target_parent_folder_id, desired);
               if(moved_sort_order_out) *moved_sort_order_out = desired;
            }
            else if(s.sort_order != desired)
            {
               FolderRepo::SetEntitySortOrder(db, entity_type, s.id, desired);
            }
         }
      }
   }

   bool IsValidBeforeKind(const std::string& s)
   {
      return s == "folder" || s == "entity";
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

   //--- PATCH /api/folders/:id/move
   //--- body: { parent_id: number|null, before_id?: number|null, before_kind?: "folder"|"entity" }
   //--- Drag-drop reposition for a folder. `before` (id + kind) may now point
   //--- to ANY sibling at the new parent's level — another folder or an
   //--- entity. ApplyMixedReorder renumbers folders and entities together so
   //--- folders are no longer pinned to the bottom of the list.
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

      //--- before is now (kind, id). Default kind is "folder" for backward compat.
      int64_t     before_id   = 0;
      std::string before_kind = "folder";
      if(j.contains("before_id") && !j["before_id"].is_null())
      {
         if(!j["before_id"].is_number_integer())
            { SendError(res, 400, "before_id must be integer or null"); return; }
         before_id = j["before_id"].get<int64_t>();
      }
      if(j.contains("before_kind") && j["before_kind"].is_string())
      {
         before_kind = j["before_kind"].get<std::string>();
         if(!IsValidBeforeKind(before_kind))
            { SendError(res, 400, "before_kind must be 'folder' or 'entity'"); return; }
      }
      if(before_id && before_kind == "folder" && before_id == id)
         { SendError(res, 400, "before_id cannot equal folder id"); return; }

      int new_sort = 0;
      ApplyMixedReorder(*ctx->db, target->entity_type, "folder", id, new_parent,
                         before_kind, before_id, &new_sort);
      auto reread = FolderRepo::Get(*ctx->db, id);
      if(!reread) { SendError(res, 500, "post-move read failed"); return; }
      res.set_content(FolderToJson(*reread).dump(), "application/json");
   });

   //--- PATCH /api/folders/move
   //--- body: { entity_type, entity_id, folder_id|null, before_id?, before_kind? }
   //--- Move/position an entity row. `before` (id + kind) places this entity
   //--- before that sibling (folder or entity) at the target folder level.
   //--- Backward compat: when before_* is missing, behaviour matches the old
   //--- "just change folder_id" semantics — sort_order is preserved.
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
      //--- Positional reorder path (preferred): client supplies before_kind+id.
      const bool wants_reorder = j.contains("before_id") || j.contains("before_kind");
      if(wants_reorder)
      {
         int64_t     before_id   = 0;
         std::string before_kind = "entity";
         if(j.contains("before_id") && !j["before_id"].is_null())
         {
            if(!j["before_id"].is_number_integer())
               { SendError(res, 400, "before_id must be integer or null"); return; }
            before_id = j["before_id"].get<int64_t>();
         }
         if(j.contains("before_kind") && j["before_kind"].is_string())
         {
            before_kind = j["before_kind"].get<std::string>();
            if(!IsValidBeforeKind(before_kind))
               { SendError(res, 400, "before_kind must be 'folder' or 'entity'"); return; }
         }
         ApplyMixedReorder(*ctx->db, et, "entity", entity_id, folder_id,
                           before_kind, before_id, nullptr);
         res.set_content(R"({"ok":true})", "application/json");
         return;
      }
      //--- Legacy path: just rebind folder_id without touching siblings.
      const bool ok = FolderRepo::Move(*ctx->db, et, entity_id, folder_id);
      if(!ok) { SendError(res, 404, "entity not found"); return; }
      res.set_content(R"({"ok":true})", "application/json");
   });
}
