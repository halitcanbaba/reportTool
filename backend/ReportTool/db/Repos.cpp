//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Repos.cpp                  |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Repos.h"
#include "../core/Crypto.h"

namespace
{
   std::string Join(const std::vector<std::string>& v, char sep)
   {
      std::string out;
      for(size_t i = 0; i < v.size(); ++i) { if(i) out += sep; out += v[i]; }
      return out;
   }

   std::vector<std::string> Split(const std::string& s, char sep)
   {
      std::vector<std::string> out;
      std::string cur;
      for(char c : s)
      {
         if(c == sep) { if(!cur.empty()) out.push_back(cur); cur.clear(); }
         else cur += c;
      }
      if(!cur.empty()) out.push_back(cur);
      return out;
   }

   void FillManagerFromStmt(SqliteStmt& st, ManagerRow& m)
   {
      m.id            = st.ColI64(0);
      m.name          = st.ColText(1);
      m.brand         = st.ColText(2);
      m.region        = st.ColText(3);
      m.server        = st.ColText(4);
      m.manager_login = st.ColU64(5);
      std::string enc = st.ColText(6);
      Crypto::DecryptB64(enc, &m.password);
      m.group_masks   = Split(st.ColText(7), ',');
      m.group_regex   = st.ColText(8);
      m.login_min     = st.IsNull(9)  ? 0 : st.ColU64(9);
      m.login_max     = st.IsNull(10) ? 0 : st.ColU64(10);
      m.active        = st.ColInt(11) != 0;
      m.created_at    = st.ColI64(12);
      m.updated_at    = st.ColI64(13);
   }
}

std::vector<ManagerRow> ManagerRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<ManagerRow> out;
   SqliteStmt st(db,
      "SELECT id,name,brand,region,server,manager_login,password_encrypted,"
      "group_masks,group_regex,login_min,login_max,active,created_at,updated_at "
      "FROM managers ORDER BY id");
   while(st.Step())
   {
      ManagerRow m; FillManagerFromStmt(st, m);
      m.regex_filters = RegexFilterRepo::Get(db, m.id);   // same lock would deadlock; use raw
      out.push_back(std::move(m));
   }
   return out;
}

std::optional<ManagerRow> ManagerRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "SELECT id,name,brand,region,server,manager_login,password_encrypted,"
      "group_masks,group_regex,login_min,login_max,active,created_at,updated_at "
      "FROM managers WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   ManagerRow m; FillManagerFromStmt(st, m);
   m.regex_filters = RegexFilterRepo::Get(db, id);
   return m;
}

namespace
{
   void BindManagerCommon(SqliteStmt& st, const ManagerRow& m, int base, bool include_password)
   {
      int idx = base;
      st.BindText(idx++, m.name);
      st.BindText(idx++, m.brand);
      st.BindText(idx++, m.region);
      st.BindText(idx++, m.server);
      st.BindU64 (idx++, m.manager_login);
      if(include_password)
         st.BindText(idx++, Crypto::EncryptB64(m.password));
      st.BindText(idx++, Join(m.group_masks, ','));
      st.BindText(idx++, m.group_regex);
      if(m.login_min) st.BindU64(idx, m.login_min); else st.BindNull(idx); idx++;
      if(m.login_max) st.BindU64(idx, m.login_max); else st.BindNull(idx); idx++;
      st.BindInt(idx++, m.active ? 1 : 0);
   }
}

int64_t ManagerRepo::Insert(SqliteDb& db, ManagerRow& m)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   db.Exec("BEGIN");
   try
   {
      SqliteStmt st(db,
         "INSERT INTO managers(name,brand,region,server,manager_login,password_encrypted,"
         "group_masks,group_regex,login_min,login_max,active,created_at,updated_at) "
         "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
      BindManagerCommon(st, m, 1, true);
      st.BindI64(12, now);
      st.BindI64(13, now);
      st.Step();
      m.id = db.LastInsertRowid();
      m.created_at = m.updated_at = now;
      RegexFilterRepo::Replace(db, m.id, m.regex_filters);
      db.Exec("COMMIT");
      return m.id;
   }
   catch(...)
   {
      db.Exec("ROLLBACK");
      throw;
   }
}

bool ManagerRepo::Update(SqliteDb& db, ManagerRow& m, bool update_password)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   db.Exec("BEGIN");
   try
   {
      const char* sql = update_password ?
         "UPDATE managers SET name=?,brand=?,region=?,server=?,manager_login=?,password_encrypted=?,"
         "group_masks=?,group_regex=?,login_min=?,login_max=?,active=?,updated_at=? WHERE id=?" :
         "UPDATE managers SET name=?,brand=?,region=?,server=?,manager_login=?,"
         "group_masks=?,group_regex=?,login_min=?,login_max=?,active=?,updated_at=? WHERE id=?";
      SqliteStmt st(db, sql);
      BindManagerCommon(st, m, 1, update_password);
      const int idx = update_password ? 12 : 11;
      st.BindI64(idx,     now);
      st.BindI64(idx + 1, m.id);
      st.Step();
      RegexFilterRepo::Replace(db, m.id, m.regex_filters);
      m.updated_at = now;
      db.Exec("COMMIT");
      return true;
   }
   catch(...) { db.Exec("ROLLBACK"); return false; }
}

bool ManagerRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM managers WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

//+--------------------- RegexFilterRepo ---------------------------+

RegexFilters RegexFilterRepo::Get(SqliteDb& db, int64_t manager_id)
{
   //--- Note: caller may already hold db.Mutex(). For simplicity we don't
   //--- re-lock; ManagerRepo helpers call within the same lock context.
   RegexFilters f;
   SqliteStmt st(db, "SELECT kind, pattern FROM regex_filters WHERE manager_id=? ORDER BY kind, sort_order");
   st.BindI64(1, manager_id);
   while(st.Step())
   {
      const std::string kind = st.ColText(0);
      const std::string p    = st.ColText(1);
      if(kind == "deposit")         f.deposit.push_back(p);
      else if(kind == "withdrawal") f.withdrawal.push_back(p);
      else if(kind == "writeoff")   f.writeoff.push_back(p);
      else if(kind == "adjustment") f.adjustment.push_back(p);
   }
   return f;
}

bool RegexFilterRepo::Replace(SqliteDb& db, int64_t manager_id, const RegexFilters& f)
{
   {
      SqliteStmt del(db, "DELETE FROM regex_filters WHERE manager_id=?");
      del.BindI64(1, manager_id); del.Step();
   }
   SqliteStmt ins(db, "INSERT INTO regex_filters(manager_id,kind,pattern,sort_order) VALUES(?,?,?,?)");
   auto write_list = [&](const char* kind, const std::vector<std::string>& v) {
      for(size_t i = 0; i < v.size(); ++i)
      {
         ins.Reset();
         ins.BindI64(1, manager_id);
         ins.BindText(2, kind);
         ins.BindText(3, v[i]);
         ins.BindInt (4, (int)i);
         ins.Step();
      }
   };
   write_list("deposit",    f.deposit);
   write_list("withdrawal", f.withdrawal);
   write_list("writeoff",   f.writeoff);
   write_list("adjustment", f.adjustment);
   return true;
}

//+--------------------- JobRepo -----------------------------------+

namespace { ReportKind ParseKind(const std::string& s) { return s == "summary" ? ReportKind::Summary : ReportKind::TopWinner; } }
namespace { JobStatus  ParseStatus(const std::string& s) {
   if(s == "running") return JobStatus::Running;
   if(s == "completed") return JobStatus::Completed;
   if(s == "failed") return JobStatus::Failed;
   return JobStatus::Queued;
} }

int64_t JobRepo::Create(SqliteDb& db, JobRow& j)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO report_jobs(manager_id,kind,params_json,status,progress,created_at) "
      "VALUES(?,?,?,?,?,?)");
   st.BindI64 (1, j.manager_id);
   st.BindText(2, ReportKindName(j.kind));
   st.BindText(3, j.params_json);
   st.BindText(4, "queued");
   st.BindReal(5, 0.0);
   st.BindI64 (6, now);
   st.Step();
   j.id = db.LastInsertRowid();
   j.status = JobStatus::Queued;
   j.created_at = now;
   return j.id;
}

bool JobRepo::UpdateStatus(SqliteDb& db, int64_t id, JobStatus s, double progress, const std::string& err)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "UPDATE report_jobs SET status=?, progress=?, error_message=?, "
      "started_at=COALESCE(started_at, CASE WHEN ?='running' THEN ? ELSE started_at END), "
      "completed_at=CASE WHEN ? IN ('completed','failed') THEN ? ELSE completed_at END "
      "WHERE id=?");
   const char* sname = JobStatusName(s);
   st.BindText(1, sname);
   st.BindReal(2, progress);
   st.BindText(3, err);
   st.BindText(4, sname);
   st.BindI64 (5, now);
   st.BindText(6, sname);
   st.BindI64 (7, now);
   st.BindI64 (8, id);
   st.Step();
   return true;
}

bool JobRepo::UpdateOutput(SqliteDb& db, int64_t id, const std::string& dir,
                            const std::string& csv, const std::string& xlsx,
                            const std::string& summary_json)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "UPDATE report_jobs SET output_dir=?, csv_filename=?, xlsx_filename=?, summary_json=? WHERE id=?");
   st.BindText(1, dir);
   st.BindText(2, csv);
   st.BindText(3, xlsx);
   st.BindText(4, summary_json);
   st.BindI64 (5, id);
   st.Step();
   return true;
}

std::optional<JobRow> JobRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "SELECT id,manager_id,kind,params_json,status,progress,error_message,"
      "output_dir,csv_filename,xlsx_filename,summary_json,created_at,started_at,completed_at "
      "FROM report_jobs WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   JobRow j;
   j.id           = st.ColI64(0);
   j.manager_id   = st.ColI64(1);
   j.kind         = ParseKind(st.ColText(2));
   j.params_json  = st.ColText(3);
   j.status       = ParseStatus(st.ColText(4));
   j.progress     = st.ColReal(5);
   j.error_message= st.ColText(6);
   j.output_dir   = st.ColText(7);
   j.csv_filename = st.ColText(8);
   j.xlsx_filename= st.ColText(9);
   j.summary_json = st.ColText(10);
   j.created_at   = st.ColI64(11);
   j.started_at   = st.IsNull(12) ? 0 : st.ColI64(12);
   j.completed_at = st.IsNull(13) ? 0 : st.ColI64(13);
   return j;
}

std::vector<JobRow> JobRepo::List(SqliteDb& db, int limit)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<JobRow> out;
   SqliteStmt st(db,
      "SELECT id,manager_id,kind,params_json,status,progress,error_message,"
      "output_dir,csv_filename,xlsx_filename,summary_json,created_at,started_at,completed_at "
      "FROM report_jobs ORDER BY id DESC LIMIT ?");
   st.BindInt(1, limit);
   while(st.Step())
   {
      JobRow j;
      j.id           = st.ColI64(0);
      j.manager_id   = st.ColI64(1);
      j.kind         = ParseKind(st.ColText(2));
      j.params_json  = st.ColText(3);
      j.status       = ParseStatus(st.ColText(4));
      j.progress     = st.ColReal(5);
      j.error_message= st.ColText(6);
      j.output_dir   = st.ColText(7);
      j.csv_filename = st.ColText(8);
      j.xlsx_filename= st.ColText(9);
      j.summary_json = st.ColText(10);
      j.created_at   = st.ColI64(11);
      j.started_at   = st.IsNull(12) ? 0 : st.ColI64(12);
      j.completed_at = st.IsNull(13) ? 0 : st.ColI64(13);
      out.push_back(std::move(j));
   }
   return out;
}

bool JobRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM report_jobs WHERE id=?");
   st.BindI64(1, id); st.Step();
   return true;
}

void JobRepo::MarkInterruptedAsFailed(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "UPDATE report_jobs SET status='failed', error_message='interrupted', "
      "completed_at=? WHERE status IN ('queued','running')");
   st.BindI64(1, (int64_t)time(nullptr));
   st.Step();
}

//+--------------------- CacheRepo ---------------------------------+

std::unordered_map<uint64_t, std::vector<DailyRow>>
CacheRepo::ReadDaily(SqliteDb& db, int64_t manager_id, int64_t day_from, int64_t day_to_excl)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::unordered_map<uint64_t, std::vector<DailyRow>> out;
   SqliteStmt st(db,
      "SELECT login,day,balance,equity,floating,daily_profit,daily_balance,daily_credit "
      "FROM daily_cache WHERE manager_id=? AND day>=? AND day<? AND sealed=1");
   st.BindI64(1, manager_id);
   st.BindI64(2, day_from);
   st.BindI64(3, day_to_excl);
   while(st.Step())
   {
      DailyRow r;
      r.login         = st.ColU64(0);
      r.datetime      = st.ColI64(1);
      r.balance       = st.ColReal(2);
      r.profit_equity = st.ColReal(3);
      r.profit        = st.ColReal(4);
      r.daily_profit  = st.ColReal(5);
      r.daily_balance = st.ColReal(6);
      r.daily_credit  = st.ColReal(7);
      out[r.login].push_back(std::move(r));
   }
   return out;
}

void CacheRepo::WriteDaily(SqliteDb& db, int64_t manager_id,
                            const std::unordered_map<uint64_t, std::vector<DailyRow>>& rows,
                            int64_t now)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   db.Exec("BEGIN");
   try
   {
      SqliteStmt st(db,
         "INSERT OR REPLACE INTO daily_cache(manager_id,login,day,balance,equity,floating,"
         "daily_profit,daily_balance,daily_credit,sealed,fetched_at) "
         "VALUES(?,?,?,?,?,?,?,?,?,?,?)");
      const int64_t seal_threshold = now - 3600 - 86400;
      for(const auto& kv : rows)
      {
         for(const auto& r : kv.second)
         {
            const int sealed = (r.datetime + 86400 < seal_threshold) ? 1 : 0;
            st.Reset();
            st.BindI64 (1,  manager_id);
            st.BindU64 (2,  r.login);
            st.BindI64 (3,  r.datetime);
            st.BindReal(4,  r.balance);
            st.BindReal(5,  r.profit_equity);
            st.BindReal(6,  r.profit);
            st.BindReal(7,  r.daily_profit);
            st.BindReal(8,  r.daily_balance);
            st.BindReal(9,  r.daily_credit);
            st.BindInt (10, sealed);
            st.BindI64 (11, now);
            st.Step();
         }
      }
      db.Exec("COMMIT");
   }
   catch(...) { db.Exec("ROLLBACK"); throw; }
}

std::unordered_map<uint64_t, std::vector<DealRow>>
CacheRepo::ReadDeals(SqliteDb& db, int64_t manager_id, int64_t time_from, int64_t time_to_excl)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::unordered_map<uint64_t, std::vector<DealRow>> out;
   SqliteStmt st(db,
      "SELECT ticket,login,action,time,profit,comment FROM deal_cache "
      "WHERE manager_id=? AND time>=? AND time<? AND sealed=1");
   st.BindI64(1, manager_id);
   st.BindI64(2, time_from);
   st.BindI64(3, time_to_excl);
   while(st.Step())
   {
      DealRow r;
      r.ticket  = st.ColU64(0);
      r.login   = st.ColU64(1);
      r.action  = (uint32_t)st.ColInt(2);
      r.time    = st.ColI64(3);
      r.profit  = st.ColReal(4);
      r.comment = st.ColText(5);
      out[r.login].push_back(std::move(r));
   }
   return out;
}

void CacheRepo::WriteDeals(SqliteDb& db, int64_t manager_id,
                            const std::unordered_map<uint64_t, std::vector<DealRow>>& rows,
                            int64_t now)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   db.Exec("BEGIN");
   try
   {
      SqliteStmt st(db,
         "INSERT OR REPLACE INTO deal_cache(manager_id,ticket,login,action,time,profit,comment,sealed,fetched_at) "
         "VALUES(?,?,?,?,?,?,?,?,?)");
      const int64_t seal_threshold = now - 3600 - 86400;
      for(const auto& kv : rows)
      {
         for(const auto& r : kv.second)
         {
            const int sealed = (r.time < seal_threshold) ? 1 : 0;
            st.Reset();
            st.BindI64 (1, manager_id);
            st.BindU64 (2, r.ticket);
            st.BindU64 (3, r.login);
            st.BindInt (4, (int)r.action);
            st.BindI64 (5, r.time);
            st.BindReal(6, r.profit);
            st.BindText(7, r.comment);
            st.BindInt (8, sealed);
            st.BindI64 (9, now);
            st.Step();
         }
      }
      db.Exec("COMMIT");
   }
   catch(...) { db.Exec("ROLLBACK"); throw; }
}
