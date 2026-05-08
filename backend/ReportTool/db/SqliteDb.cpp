//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       SqliteDb.cpp               |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "SqliteDb.h"

SqliteDb::~SqliteDb() { Close(); }

bool SqliteDb::Open(const std::string& path, std::string* err)
{
   int rc = sqlite3_open(path.c_str(), &m_db);
   if(rc != SQLITE_OK)
   {
      if(err) *err = sqlite3_errmsg(m_db ? m_db : nullptr);
      if(m_db) { sqlite3_close(m_db); m_db = nullptr; }
      return false;
   }
   const char* pragmas =
      "PRAGMA journal_mode=WAL;"
      "PRAGMA synchronous=NORMAL;"
      "PRAGMA foreign_keys=ON;"
      "PRAGMA busy_timeout=5000;";
   char* errmsg = nullptr;
   if(sqlite3_exec(m_db, pragmas, nullptr, nullptr, &errmsg) != SQLITE_OK)
   {
      if(err) *err = errmsg ? errmsg : "PRAGMA failed";
      sqlite3_free(errmsg);
      return false;
   }
   return true;
}

void SqliteDb::Close()
{
   if(m_db) { sqlite3_close(m_db); m_db = nullptr; }
}

bool SqliteDb::Exec(const std::string& sql, std::string* err)
{
   if(!m_db) return false;
   char* errmsg = nullptr;
   int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errmsg);
   if(rc != SQLITE_OK)
   {
      if(err) *err = errmsg ? errmsg : "exec failed";
      sqlite3_free(errmsg);
      return false;
   }
   return true;
}

bool SqliteDb::Begin()    { return Exec("BEGIN"); }
bool SqliteDb::Commit()   { return Exec("COMMIT"); }
bool SqliteDb::Rollback() { return Exec("ROLLBACK"); }

int64_t SqliteDb::LastInsertRowid() { return sqlite3_last_insert_rowid(m_db); }

//+--------------------- SqliteStmt --------------------------------+

SqliteStmt::SqliteStmt(SqliteDb& db, const std::string& sql)
{
   int rc = sqlite3_prepare_v2(db.Handle(), sql.c_str(), -1, &m_stmt, nullptr);
   if(rc != SQLITE_OK)
      throw SqlError(std::string("prepare failed: ") + sqlite3_errmsg(db.Handle()) + " sql=" + sql);
}

SqliteStmt::~SqliteStmt() { if(m_stmt) sqlite3_finalize(m_stmt); }

void SqliteStmt::BindNull(int idx)               { sqlite3_bind_null(m_stmt, idx); }
void SqliteStmt::BindInt (int idx, int v)        { sqlite3_bind_int(m_stmt, idx, v); }
void SqliteStmt::BindI64 (int idx, int64_t v)    { sqlite3_bind_int64(m_stmt, idx, v); }
void SqliteStmt::BindU64 (int idx, uint64_t v)   { sqlite3_bind_int64(m_stmt, idx, (sqlite3_int64)v); }
void SqliteStmt::BindReal(int idx, double v)     { sqlite3_bind_double(m_stmt, idx, v); }
void SqliteStmt::BindText(int idx, const std::string& s)
{
   sqlite3_bind_text(m_stmt, idx, s.data(), (int)s.size(), SQLITE_TRANSIENT);
}
void SqliteStmt::BindBlob(int idx, const void* data, size_t len)
{
   sqlite3_bind_blob(m_stmt, idx, data, (int)len, SQLITE_TRANSIENT);
}

bool SqliteStmt::Step()
{
   int rc = sqlite3_step(m_stmt);
   if(rc == SQLITE_ROW)  return true;
   if(rc == SQLITE_DONE) return false;
   throw SqlError(std::string("step failed: rc=") + std::to_string(rc));
}

bool         SqliteStmt::IsNull(int c) const { return sqlite3_column_type(m_stmt, c) == SQLITE_NULL; }
int          SqliteStmt::ColInt (int c) const { return sqlite3_column_int(m_stmt, c); }
int64_t      SqliteStmt::ColI64 (int c) const { return sqlite3_column_int64(m_stmt, c); }
uint64_t     SqliteStmt::ColU64 (int c) const { return (uint64_t)sqlite3_column_int64(m_stmt, c); }
double       SqliteStmt::ColReal(int c) const { return sqlite3_column_double(m_stmt, c); }
std::string  SqliteStmt::ColText(int c) const
{
   const unsigned char* p = sqlite3_column_text(m_stmt, c);
   int n = sqlite3_column_bytes(m_stmt, c);
   if(!p || n == 0) return "";
   return std::string((const char*)p, n);
}
std::vector<uint8_t> SqliteStmt::ColBlob(int c) const
{
   const void* p = sqlite3_column_blob(m_stmt, c);
   int n = sqlite3_column_bytes(m_stmt, c);
   if(!p || n == 0) return {};
   return std::vector<uint8_t>((const uint8_t*)p, (const uint8_t*)p + n);
}

void SqliteStmt::Reset() { sqlite3_reset(m_stmt); sqlite3_clear_bindings(m_stmt); }
