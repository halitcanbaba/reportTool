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

   //--- on insert/update: row.password (plain) is encrypted before storage.
   //--- regex_filters are written via RegexFilterRepo helper inside the same txn.
   //--- Returns the assigned id (>0) on insert; same id on update.
   int64_t Insert(SqliteDb& db, ManagerRow& row);
   bool    Update(SqliteDb& db, ManagerRow& row, bool update_password);
   bool    Delete(SqliteDb& db, int64_t id);
}

namespace RegexFilterRepo
{
   //--- read all 4 categories for one manager.
   RegexFilters Get(SqliteDb& db, int64_t manager_id);

   //--- replaces all filters for the manager (delete-then-insert in caller's txn).
   bool Replace(SqliteDb& db, int64_t manager_id, const RegexFilters& f);
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

   //--- mark any 'running' or 'queued' rows as 'failed' on startup recovery.
   void    MarkInterruptedAsFailed(SqliteDb& db);
}

namespace CacheRepo
{
   //--- daily_cache reads / writes.
   //--- ReadDaily returns map<login, vector<DailyRow sorted by day>> for sealed days only;
   //--- caller refetches days that are not sealed yet.
   std::unordered_map<uint64_t, std::vector<DailyRow>>
   ReadDaily(SqliteDb& db, int64_t manager_id, int64_t day_from, int64_t day_to_excl);

   //--- upserts a batch (uses INSERT OR REPLACE).
   void WriteDaily(SqliteDb& db, int64_t manager_id,
                   const std::unordered_map<uint64_t, std::vector<DailyRow>>& rows,
                   int64_t now);

   //--- balance/correction-only deal cache.
   std::unordered_map<uint64_t, std::vector<DealRow>>
   ReadDeals(SqliteDb& db, int64_t manager_id, int64_t time_from, int64_t time_to_excl);

   void WriteDeals(SqliteDb& db, int64_t manager_id,
                   const std::unordered_map<uint64_t, std::vector<DealRow>>& rows,
                   int64_t now);
}
