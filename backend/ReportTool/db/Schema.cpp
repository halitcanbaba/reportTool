#include "../stdafx.h"
#include "Schema.h"

namespace
{
   const char* kSchema =
      "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY);"

      "CREATE TABLE IF NOT EXISTS managers ("
      "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name               TEXT NOT NULL,"
      "  brand              TEXT NOT NULL,"
      "  region             TEXT NOT NULL,"
      "  server             TEXT NOT NULL,"
      "  manager_login      INTEGER NOT NULL,"
      "  password_encrypted TEXT NOT NULL,"
      "  group_masks        TEXT NOT NULL DEFAULT '',"
      "  group_regex        TEXT NOT NULL DEFAULT '',"
      "  login_min          INTEGER,"
      "  login_max          INTEGER,"
      "  active             INTEGER NOT NULL DEFAULT 1,"
      "  created_at         INTEGER NOT NULL,"
      "  updated_at         INTEGER NOT NULL"
      ");"

      "CREATE TABLE IF NOT EXISTS regex_filters ("
      "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  manager_id  INTEGER NOT NULL,"
      "  kind        TEXT    NOT NULL CHECK(kind IN ('deposit','withdrawal','writeoff','adjustment')),"
      "  pattern     TEXT    NOT NULL,"
      "  sort_order  INTEGER NOT NULL DEFAULT 0,"
      "  FOREIGN KEY (manager_id) REFERENCES managers(id) ON DELETE CASCADE"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_regex_filters_manager_kind ON regex_filters(manager_id, kind);"

      "CREATE TABLE IF NOT EXISTS report_jobs ("
      "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  manager_id    INTEGER NOT NULL,"
      "  kind          TEXT    NOT NULL CHECK(kind IN ('top_winner','summary')),"
      "  params_json   TEXT    NOT NULL,"
      "  status        TEXT    NOT NULL CHECK(status IN ('queued','running','completed','failed')),"
      "  progress      REAL    NOT NULL DEFAULT 0.0,"
      "  error_message TEXT,"
      "  output_dir    TEXT,"
      "  csv_filename  TEXT,"
      "  xlsx_filename TEXT,"
      "  summary_json  TEXT,"
      "  created_at    INTEGER NOT NULL,"
      "  started_at    INTEGER,"
      "  completed_at  INTEGER,"
      "  FOREIGN KEY (manager_id) REFERENCES managers(id) ON DELETE CASCADE"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_report_jobs_status_created ON report_jobs(status, created_at);"

      "CREATE TABLE IF NOT EXISTS daily_cache ("
      "  manager_id    INTEGER NOT NULL,"
      "  login         INTEGER NOT NULL,"
      "  day           INTEGER NOT NULL,"
      "  balance       REAL    NOT NULL,"
      "  equity        REAL    NOT NULL,"
      "  floating      REAL    NOT NULL,"
      "  daily_profit  REAL    NOT NULL,"
      "  daily_balance REAL    NOT NULL,"
      "  daily_credit  REAL    NOT NULL,"
      "  sealed        INTEGER NOT NULL DEFAULT 0,"
      "  fetched_at    INTEGER NOT NULL,"
      "  PRIMARY KEY (manager_id, login, day)"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_daily_cache_range ON daily_cache(manager_id, day);"

      "CREATE TABLE IF NOT EXISTS deal_cache ("
      "  manager_id INTEGER NOT NULL,"
      "  ticket     INTEGER NOT NULL,"
      "  login      INTEGER NOT NULL,"
      "  action     INTEGER NOT NULL,"
      "  time       INTEGER NOT NULL,"
      "  profit     REAL    NOT NULL,"
      "  comment    TEXT,"
      "  sealed     INTEGER NOT NULL DEFAULT 0,"
      "  fetched_at INTEGER NOT NULL,"
      "  PRIMARY KEY (manager_id, ticket)"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_deal_cache_login_time ON deal_cache(manager_id, login, time);"
      "CREATE INDEX IF NOT EXISTS ix_deal_cache_time       ON deal_cache(manager_id, time);";
}

bool Schema::Apply(SqliteDb& db, std::string* err)
{
   if(!db.Exec(kSchema, err)) return false;
   //--- seed schema_version row if missing
   if(!db.Exec("INSERT OR IGNORE INTO schema_version(version) VALUES (1);", err)) return false;
   return true;
}
