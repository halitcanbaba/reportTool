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
