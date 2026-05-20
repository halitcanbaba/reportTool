//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                  Repos.h - typed CRUD over SQLite                |
//+------------------------------------------------------------------+
#pragma once
#include "SqliteDb.h"
#include "../core/Records.h"
#include <vector>
#include <optional>

namespace ManagerRepo
{
   //--- store/fetch ManagerRow. password is plaintext on the row; this layer
   //--- handles encrypt-on-write / decrypt-on-read via Crypto.
   std::vector<ManagerRow> ListAll(SqliteDb& db);
   std::optional<ManagerRow> Get(SqliteDb& db, int64_t id);

   int64_t Insert(SqliteDb& db, ManagerRow& row);
   bool    Update(SqliteDb& db, ManagerRow& row, bool update_password);
   bool    Delete(SqliteDb& db, int64_t id);
}

namespace RegexFilterRepo
{
   RegexFilters Get(SqliteDb& db, int64_t manager_id);
   bool Replace(SqliteDb& db, int64_t manager_id, const RegexFilters& f);
}

namespace AccountFilterRepo
{
   std::vector<AccountFilter>  ListAll(SqliteDb& db);
   std::optional<AccountFilter> Get(SqliteDb& db, int64_t id);
   int64_t Insert(SqliteDb& db, AccountFilter& f);
   bool    Update(SqliteDb& db, AccountFilter& f);
   bool    Delete(SqliteDb& db, int64_t id);
}

namespace TemplateRepo
{
   std::vector<ReportTemplate>  ListAll(SqliteDb& db);
   std::optional<ReportTemplate> Get(SqliteDb& db, int64_t id);
   int64_t Insert(SqliteDb& db, ReportTemplate& t);
   bool    Update(SqliteDb& db, ReportTemplate& t);
   bool    Delete(SqliteDb& db, int64_t id);
}

namespace BlueprintRepo
{
   std::vector<FormulaBlueprint>  ListAll(SqliteDb& db);
   std::optional<FormulaBlueprint> Get(SqliteDb& db, int64_t id);
   int64_t Insert(SqliteDb& db, FormulaBlueprint& b);
   bool    Update(SqliteDb& db, FormulaBlueprint& b);
   bool    Delete(SqliteDb& db, int64_t id);
}

namespace ReadyMadeRepo
{
   std::vector<ReadyMadeReport>  ListAll(SqliteDb& db);
   std::optional<ReadyMadeReport> Get(SqliteDb& db, int64_t id);
   int64_t Insert(SqliteDb& db, ReadyMadeReport& r);
   bool    Update(SqliteDb& db, ReadyMadeReport& r);
   bool    Delete(SqliteDb& db, int64_t id);
}

namespace ScheduleRepo
{
   std::vector<ScheduleEntry>  ListAll(SqliteDb& db);
   std::optional<ScheduleEntry> Get(SqliteDb& db, int64_t id);
   int64_t Insert(SqliteDb& db, ScheduleEntry& s);
   bool    Update(SqliteDb& db, ScheduleEntry& s);
   bool    Delete(SqliteDb& db, int64_t id);

   //--- Scheduler-side helpers
   std::vector<ScheduleEntry> ListDue(SqliteDb& db, int64_t now);          // enabled && next_run_at <= now && last_status != 'dispatched'
   std::vector<ScheduleEntry> ListDispatched(SqliteDb& db);                // last_status == 'dispatched'
   bool UpdateDispatch(SqliteDb& db, int64_t id, int64_t last_run_at,
                       int64_t next_run_at, int64_t last_job_id);
   bool UpdateDelivery(SqliteDb& db, int64_t id, const std::string& status,
                       const std::string& last_error);
}

namespace SettingsRepo
{
   std::string Get(SqliteDb& db, const std::string& key);
   void        Set(SqliteDb& db, const std::string& key, const std::string& value);
}

namespace FolderRepo
{
   //--- Generic organisational folder across five entity types. Distinguished
   //--- by `entity_type`. `item_count` is populated by ListByEntity, not stored.
   std::vector<FolderRow>  ListByEntity(SqliteDb& db, const std::string& entity_type);
   std::optional<FolderRow> Get(SqliteDb& db, int64_t id);
   int64_t Insert(SqliteDb& db, FolderRow& f);
   bool    Update(SqliteDb& db, FolderRow& f);
   bool    Delete(SqliteDb& db, int64_t id);   // child rows fall to Unfiled via SET NULL
   //--- Move an entity row to a folder (folder_id=0 → NULL = unfiled).
   bool    Move(SqliteDb& db, const std::string& entity_type,
                int64_t entity_id, int64_t folder_id);

   //--- v13: cross-table sort utilities. The mixed move handler in
   //--- FolderRoutes uses these to enumerate + renumber every sibling
   //--- (folder + entity) at a level so folders and entities can be
   //--- intermixed freely in the user's chosen order.
   struct LevelEntity { int64_t id; int sort_order; };
   std::vector<LevelEntity> ListEntitiesAtLevel(SqliteDb& db,
                                                const std::string& entity_type,
                                                int64_t folder_id /* 0 = root */);
   bool    SetEntitySortOrder(SqliteDb& db, const std::string& entity_type,
                              int64_t entity_id, int sort_order);
   bool    MoveEntityWithOrder(SqliteDb& db, const std::string& entity_type,
                               int64_t entity_id, int64_t folder_id, int sort_order);
}

namespace UserRepo
{
   std::vector<User>           ListAll(SqliteDb& db);
   std::optional<User>         Get(SqliteDb& db, int64_t id);
   std::optional<User>         GetByUsername(SqliteDb& db, const std::string& username);
   int64_t                     Insert(SqliteDb& db, User& u);
   bool                        UpdateRoleActive(SqliteDb& db, int64_t id, const std::string& role, bool active);
   bool                        UpdatePassword(SqliteDb& db, int64_t id, const std::string& password_hash);
   //--- Stamps users.last_login_at (column name is legacy; semantically
   //--- "last active"). Called by the HTTP pre-routing handler with a 60s
   //--- per-user throttle, and on successful login as a fresh touch.
   bool                        UpdateLastActive(SqliteDb& db, int64_t id, int64_t when);
   bool                        Delete(SqliteDb& db, int64_t id);
   int64_t                     Count(SqliteDb& db);
   int64_t                     CountActiveAdmins(SqliteDb& db);
}

namespace SessionRepo
{
   bool                        Insert(SqliteDb& db, const Session& s);
   std::optional<Session>      Get(SqliteDb& db, const std::string& token);
   bool                        Touch(SqliteDb& db, const std::string& token, int64_t new_expires_at);
   bool                        Delete(SqliteDb& db, const std::string& token);
   int64_t                     DeleteByUser(SqliteDb& db, int64_t user_id);
   //--- Drop all sessions for a user EXCEPT one (used on password change to
   //--- keep the caller's own session alive while invalidating others).
   int64_t                     DeleteByUserExcept(SqliteDb& db, int64_t user_id, const std::string& keep_token);
   int64_t                     DeleteExpired(SqliteDb& db, int64_t now);
}

namespace JobRepo
{
   int64_t Create(SqliteDb& db, JobRow& job);   // assigns id
   bool    UpdateStatus(SqliteDb& db, int64_t id, JobStatus s, double progress, const std::string& err = "");
   bool    UpdateOutput(SqliteDb& db, int64_t id, const std::string& output_dir,
                        const std::string& csv, const std::string& xlsx, const std::string& summary_json);
   std::optional<JobRow> Get(SqliteDb& db, int64_t id);
   std::vector<JobRow>   List(SqliteDb& db, int limit);
   bool    Delete(SqliteDb& db, int64_t id);

   void    MarkInterruptedAsFailed(SqliteDb& db);
}

namespace CacheRepo
{
   //--- (currently not wired up — kept for v2.)
   std::unordered_map<uint64_t, std::vector<DailyRow>>
   ReadDaily(SqliteDb& db, int64_t manager_id, int64_t day_from, int64_t day_to_excl);

   void WriteDaily(SqliteDb& db, int64_t manager_id,
                   const std::unordered_map<uint64_t, std::vector<DailyRow>>& rows,
                   int64_t now);

   std::unordered_map<uint64_t, std::vector<DealRow>>
   ReadDeals(SqliteDb& db, int64_t manager_id, int64_t time_from, int64_t time_to_excl);

   void WriteDeals(SqliteDb& db, int64_t manager_id,
                   const std::unordered_map<uint64_t, std::vector<DealRow>>& rows,
                   int64_t now);
}
