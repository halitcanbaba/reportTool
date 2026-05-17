#include "../stdafx.h"
#include "Schema.h"

namespace
{
   constexpr int kCurrentSchemaVersion = 8;

   //--- Tables that survive every version (managers, regex_filters,
   //--- daily_cache, deal_cache). These are idempotent.
   const char* kCommonTables =
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

   //--- v2 tables: account_filters, report_templates, report_jobs (rebuilt).
   const char* kV2Tables =
      "CREATE TABLE IF NOT EXISTS account_filters ("
      "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name         TEXT NOT NULL,"
      "  description  TEXT NOT NULL DEFAULT '',"
      "  group_masks  TEXT NOT NULL DEFAULT '',"
      "  group_regex  TEXT NOT NULL DEFAULT '',"
      "  login_min    INTEGER,"
      "  login_max    INTEGER,"
      "  manager_id   INTEGER,"
      "  created_at   INTEGER NOT NULL,"
      "  updated_at   INTEGER NOT NULL,"
      "  FOREIGN KEY (manager_id) REFERENCES managers(id) ON DELETE CASCADE"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_account_filters_manager ON account_filters(manager_id);"

      "CREATE TABLE IF NOT EXISTS report_templates ("
      "  id             INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name           TEXT NOT NULL,"
      "  description    TEXT NOT NULL DEFAULT '',"
      "  row_model      TEXT NOT NULL DEFAULT 'per_account',"
      "  date_params    TEXT NOT NULL DEFAULT '[]',"
      "  columns_json   TEXT NOT NULL,"
      "  sort_json      TEXT NOT NULL DEFAULT '{}',"
      "  default_top_n  INTEGER NOT NULL DEFAULT 0,"
      "  created_at     INTEGER NOT NULL,"
      "  updated_at     INTEGER NOT NULL"
      ");"

      "CREATE TABLE IF NOT EXISTS report_jobs ("
      "  id                INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  manager_id        INTEGER NOT NULL,"
      "  template_id       INTEGER NOT NULL,"
      "  account_filter_id INTEGER,"
      "  params_json       TEXT    NOT NULL,"
      "  status            TEXT    NOT NULL CHECK(status IN ('queued','running','completed','failed')),"
      "  progress          REAL    NOT NULL DEFAULT 0.0,"
      "  error_message     TEXT,"
      "  output_dir        TEXT,"
      "  csv_filename      TEXT,"
      "  xlsx_filename     TEXT,"
      "  summary_json      TEXT,"
      "  created_at        INTEGER NOT NULL,"
      "  started_at        INTEGER,"
      "  completed_at      INTEGER,"
      "  FOREIGN KEY (manager_id)        REFERENCES managers(id)         ON DELETE CASCADE,"
      "  FOREIGN KEY (template_id)       REFERENCES report_templates(id) ON DELETE RESTRICT,"
      "  FOREIGN KEY (account_filter_id) REFERENCES account_filters(id)  ON DELETE SET NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_report_jobs_status_created ON report_jobs(status, created_at);"
      "CREATE INDEX IF NOT EXISTS ix_report_jobs_template       ON report_jobs(template_id);";

   //--- v3 tables: formula_blueprints.
   const char* kV3Tables =
      "CREATE TABLE IF NOT EXISTS formula_blueprints ("
      "  id           INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name         TEXT NOT NULL UNIQUE,"
      "  description  TEXT NOT NULL DEFAULT '',"
      "  date_params  TEXT NOT NULL DEFAULT '[]',"
      "  expr_json    TEXT NOT NULL,"
      "  created_at   INTEGER NOT NULL,"
      "  updated_at   INTEGER NOT NULL"
      ");";

   int ReadVersion(SqliteDb& db)
   {
      SqliteStmt st(db, "SELECT version FROM schema_version LIMIT 1");
      if(!st.Step()) return 0;
      return st.ColInt(0);
   }

   bool WriteVersion(SqliteDb& db, int v, std::string* err)
   {
      if(!db.Exec("DELETE FROM schema_version", err)) return false;
      char buf[64];
      snprintf(buf, sizeof(buf), "INSERT INTO schema_version(version) VALUES(%d)", v);
      return db.Exec(buf, err);
   }

   //--- Detect whether the OLD v1 report_jobs table is present (has 'kind').
   bool ReportJobsHasKindColumn(SqliteDb& db)
   {
      SqliteStmt st(db, "PRAGMA table_info(report_jobs)");
      while(st.Step())
      {
         if(st.ColText(1) == "kind") return true;
      }
      return false;
   }

   //--- Migrate v1 → v2: rebuild report_jobs without 'kind', drop old job rows.
   bool MigrateToV2(SqliteDb& db, std::string* err)
   {
      //--- old report_jobs (with 'kind' CHECK) must be replaced. Best effort:
      //--- drop the existing table (the data refers to old report kinds that
      //--- can no longer be re-run). New schema is created by kV2Tables.
      if(ReportJobsHasKindColumn(db))
      {
         if(!db.Exec("DROP TABLE IF EXISTS report_jobs", err)) return false;
      }
      if(!db.Exec(kV2Tables, err)) return false;
      return WriteVersion(db, 2, err);
   }

   //--- v2 → v3: add formula_blueprints table.
   bool MigrateToV3(SqliteDb& db, std::string* err)
   {
      if(!db.Exec(kV3Tables, err)) return false;
      return WriteVersion(db, 3, err);
   }

   bool AccountFiltersHasUserPredicateColumn(SqliteDb& db)
   {
      SqliteStmt st(db, "PRAGMA table_info(account_filters)");
      while(st.Step())
         if(st.ColText(1) == "user_predicate_json") return true;
      return false;
   }

   //--- v3 → v4: add account_filters.user_predicate_json column.
   bool MigrateToV4(SqliteDb& db, std::string* err)
   {
      if(!AccountFiltersHasUserPredicateColumn(db))
      {
         if(!db.Exec("ALTER TABLE account_filters ADD COLUMN user_predicate_json TEXT NOT NULL DEFAULT ''", err))
            return false;
      }
      return WriteVersion(db, 4, err);
   }

   //--- v5 tables: ready_made_reports, schedules, app_settings.
   const char* kV5Tables =
      "CREATE TABLE IF NOT EXISTS ready_made_reports ("
      "  id                INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name              TEXT NOT NULL,"
      "  description       TEXT NOT NULL DEFAULT '',"
      "  template_id       INTEGER NOT NULL,"
      "  account_filter_id INTEGER,"
      "  date_strategy     TEXT NOT NULL DEFAULT 'relative',"
      "  fixed_dates_json  TEXT NOT NULL DEFAULT '{}',"
      "  relative_preset   TEXT NOT NULL DEFAULT 'last_n_days',"
      "  relative_n        INTEGER NOT NULL DEFAULT 7,"
      "  top_n_override    INTEGER NOT NULL DEFAULT 0,"
      "  created_at        INTEGER NOT NULL,"
      "  updated_at        INTEGER NOT NULL,"
      "  FOREIGN KEY (template_id)       REFERENCES report_templates(id) ON DELETE RESTRICT,"
      "  FOREIGN KEY (account_filter_id) REFERENCES account_filters(id)  ON DELETE SET NULL"
      ");"

      "CREATE TABLE IF NOT EXISTS schedules ("
      "  id                 INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  name               TEXT NOT NULL,"
      "  ready_made_id      INTEGER NOT NULL,"
      "  frequency          TEXT NOT NULL DEFAULT 'daily',"
      "  time_hour          INTEGER NOT NULL DEFAULT 8,"
      "  time_minute        INTEGER NOT NULL DEFAULT 0,"
      "  day_of_week        INTEGER NOT NULL DEFAULT 1,"
      "  day_of_month       INTEGER NOT NULL DEFAULT 1,"
      "  every_n_hours      INTEGER NOT NULL DEFAULT 1,"
      "  telegram_chat_id   TEXT NOT NULL DEFAULT '',"
      "  enabled            INTEGER NOT NULL DEFAULT 1,"
      "  next_run_at        INTEGER NOT NULL DEFAULT 0,"
      "  last_run_at        INTEGER NOT NULL DEFAULT 0,"
      "  last_status        TEXT NOT NULL DEFAULT '',"
      "  last_job_id        INTEGER,"
      "  last_error         TEXT NOT NULL DEFAULT '',"
      "  created_at         INTEGER NOT NULL,"
      "  updated_at         INTEGER NOT NULL,"
      "  FOREIGN KEY (ready_made_id) REFERENCES ready_made_reports(id) ON DELETE RESTRICT,"
      "  FOREIGN KEY (last_job_id)   REFERENCES report_jobs(id)        ON DELETE SET NULL"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_schedules_next ON schedules(enabled, next_run_at);"

      "CREATE TABLE IF NOT EXISTS app_settings ("
      "  key   TEXT PRIMARY KEY,"
      "  value TEXT NOT NULL DEFAULT ''"
      ");";

   //--- v4 → v5: add ready_made_reports, schedules, app_settings tables.
   bool MigrateToV5(SqliteDb& db, std::string* err)
   {
      if(!db.Exec(kV5Tables, err)) return false;
      return WriteVersion(db, 5, err);
   }

   //--- v6 tables: users, sessions. Auth/RBAC foundation.
   const char* kV6Tables =
      "CREATE TABLE IF NOT EXISTS users ("
      "  id            INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  username      TEXT    NOT NULL UNIQUE,"
      "  password_hash TEXT    NOT NULL,"
      "  role          TEXT    NOT NULL DEFAULT 'viewer' CHECK(role IN ('admin','viewer')),"
      "  active        INTEGER NOT NULL DEFAULT 1,"
      "  created_at    INTEGER NOT NULL,"
      "  updated_at    INTEGER NOT NULL,"
      "  last_login_at INTEGER NOT NULL DEFAULT 0"
      ");"

      "CREATE TABLE IF NOT EXISTS sessions ("
      "  token       TEXT    PRIMARY KEY,"
      "  user_id     INTEGER NOT NULL,"
      "  created_at  INTEGER NOT NULL,"
      "  expires_at  INTEGER NOT NULL,"
      "  remote_addr TEXT    NOT NULL DEFAULT '',"
      "  user_agent  TEXT    NOT NULL DEFAULT '',"
      "  FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE"
      ");"
      "CREATE INDEX IF NOT EXISTS ix_sessions_expires ON sessions(expires_at);"
      "CREATE INDEX IF NOT EXISTS ix_sessions_user    ON sessions(user_id);";

   //--- v5 → v6: add users + sessions tables.
   bool MigrateToV6(SqliteDb& db, std::string* err)
   {
      if(!db.Exec(kV6Tables, err)) return false;
      return WriteVersion(db, 6, err);
   }

   //--- v6 → v7: reset default_top_n from legacy default 20 to 0 (no limit).
   bool MigrateToV7(SqliteDb& db, std::string* err)
   {
      if(!db.Exec("UPDATE report_templates SET default_top_n=0 WHERE default_top_n=20", err)) return false;
      return WriteVersion(db, 7, err);
   }

   //--- v7 → v8: add `delivery_format` to schedules so each schedule can
   //--- choose between sending the CSV file or a brief text summary.
   bool MigrateToV8(SqliteDb& db, std::string* err)
   {
      if(!db.Exec("ALTER TABLE schedules ADD COLUMN delivery_format TEXT NOT NULL DEFAULT 'csv'", err))
         return false;
      return WriteVersion(db, 8, err);
   }
}

bool Schema::Apply(SqliteDb& db, std::string* err)
{
   //--- Common tables: managers, regex_filters, caches, schema_version.
   if(!db.Exec(kCommonTables, err)) return false;

   const int v = ReadVersion(db);
   if(v == 0)
   {
      //--- Fresh database: create all current-version tables and seed.
      if(!db.Exec(kV2Tables, err)) return false;
      if(!db.Exec(kV3Tables, err)) return false;
      //--- v4 column lives inside the v2 account_filters table definition for
      //--- fresh installs only via this ALTER; future fresh-install consolidation
      //--- can move it into kV2Tables when convenient.
      if(!db.Exec("ALTER TABLE account_filters ADD COLUMN user_predicate_json TEXT NOT NULL DEFAULT ''", err)) return false;
      if(!db.Exec(kV5Tables, err)) return false;
      if(!db.Exec(kV6Tables, err)) return false;
      //--- v8 column on the fresh-install path (kV5Tables hasn't been
      //--- consolidated yet — kept as ALTER to mirror the migration).
      if(!db.Exec("ALTER TABLE schedules ADD COLUMN delivery_format TEXT NOT NULL DEFAULT 'csv'", err)) return false;
      return WriteVersion(db, kCurrentSchemaVersion, err);
   }
   if(v < 2)
   {
      if(!MigrateToV2(db, err)) return false;
   }
   if(v < 3)
   {
      if(!MigrateToV3(db, err)) return false;
   }
   if(v < 4)
   {
      if(!MigrateToV4(db, err)) return false;
   }
   if(v < 5)
   {
      if(!MigrateToV5(db, err)) return false;
   }
   if(v < 6)
   {
      if(!MigrateToV6(db, err)) return false;
   }
   if(v < 7)
   {
      if(!MigrateToV7(db, err)) return false;
   }
   if(v < 8)
   {
      if(!MigrateToV8(db, err)) return false;
   }
   //--- v >= current: no-op.
   return true;
}
