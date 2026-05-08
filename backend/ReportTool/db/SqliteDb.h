//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|              SqliteDb.h - RAII wrapper for sqlite3*              |
//+------------------------------------------------------------------+
#pragma once
#include "../third_party/sqlite3.h"
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <stdexcept>

class SqliteStmt;

class SqliteDb
{
public:
   SqliteDb() = default;
   ~SqliteDb();

   //--- opens path; sets WAL, busy_timeout, foreign_keys.
   bool Open(const std::string& path, std::string* err = nullptr);
   void Close();
   bool IsOpen() const { return m_db != nullptr; }

   //--- execute a single statement (no results).
   bool Exec(const std::string& sql, std::string* err = nullptr);

   //--- begin/commit/rollback (manual transaction control)
   bool Begin();
   bool Commit();
   bool Rollback();

   sqlite3* Handle() { return m_db; }
   std::mutex& Mutex() { return m_mu; }

   int64_t LastInsertRowid();

private:
   sqlite3*   m_db = nullptr;
   std::mutex m_mu;
};

//--- thin RAII wrapper for prepared statements.
class SqliteStmt
{
public:
   SqliteStmt(SqliteDb& db, const std::string& sql);
   ~SqliteStmt();

   SqliteStmt(const SqliteStmt&) = delete;
   SqliteStmt& operator=(const SqliteStmt&) = delete;

   //--- bind by 1-based index
   void BindNull (int idx);
   void BindInt  (int idx, int v);
   void BindI64  (int idx, int64_t v);
   void BindU64  (int idx, uint64_t v);
   void BindReal (int idx, double v);
   void BindText (int idx, const std::string& s);
   void BindBlob (int idx, const void* data, size_t len);

   //--- step. true = SQLITE_ROW (more rows). false = SQLITE_DONE or error.
   bool Step();

   //--- column getters by 0-based index
   bool         IsNull(int col) const;
   int          ColInt(int col) const;
   int64_t      ColI64(int col) const;
   uint64_t     ColU64(int col) const;
   double       ColReal(int col) const;
   std::string  ColText(int col) const;
   std::vector<uint8_t> ColBlob(int col) const;

   void Reset();

   sqlite3_stmt* Raw() { return m_stmt; }

private:
   sqlite3_stmt* m_stmt = nullptr;
};

class SqlError : public std::runtime_error
{
public:
   explicit SqlError(const std::string& m) : std::runtime_error(m) {}
};
