//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|                                       Repos.cpp                  |
//+------------------------------------------------------------------+
#include "../stdafx.h"
#include "Repos.h"
#include "../core/Crypto.h"
#include "../reports/Expression.h"

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

//+--------------------- ManagerRepo --------------------------------+

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
      m.regex_filters = RegexFilterRepo::Get(db, m.id);
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

//+--------------------- AccountFilterRepo -------------------------+

namespace
{
   void FillAccountFilterFromStmt(SqliteStmt& st, AccountFilter& f)
   {
      f.id          = st.ColI64(0);
      f.name        = st.ColText(1);
      f.description = st.ColText(2);
      f.group_masks = Split(st.ColText(3), ',');
      f.group_regex = st.ColText(4);
      f.login_min   = st.IsNull(5) ? 0 : st.ColU64(5);
      f.login_max   = st.IsNull(6) ? 0 : st.ColU64(6);
      f.manager_id  = st.IsNull(7) ? 0 : st.ColI64(7);
      f.created_at  = st.ColI64(8);
      f.updated_at  = st.ColI64(9);
      const std::string upj = st.ColText(10);
      if(!upj.empty())
      {
         nlohmann::json j = nlohmann::json::parse(upj, nullptr, false);
         if(!j.is_discarded())
         {
            std::string err;
            Expression::PredicateFromJson(j, &f.user_predicate, &err);
         }
      }
   }

   //--- Inline JSON of user_predicate; empty string if null.
   std::string AccountFilterUserPredJson(const AccountFilter& f)
   {
      if(!f.user_predicate) return "";
      return Expression::PredicateToJson(*f.user_predicate).dump();
   }
}

std::vector<AccountFilter> AccountFilterRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<AccountFilter> out;
   SqliteStmt st(db,
      "SELECT id,name,description,group_masks,group_regex,login_min,login_max,manager_id,"
      "created_at,updated_at,user_predicate_json FROM account_filters ORDER BY id");
   while(st.Step())
   {
      AccountFilter f; FillAccountFilterFromStmt(st, f);
      out.push_back(std::move(f));
   }
   return out;
}

std::optional<AccountFilter> AccountFilterRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "SELECT id,name,description,group_masks,group_regex,login_min,login_max,manager_id,"
      "created_at,updated_at,user_predicate_json FROM account_filters WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   AccountFilter f; FillAccountFilterFromStmt(st, f);
   return f;
}

int64_t AccountFilterRepo::Insert(SqliteDb& db, AccountFilter& f)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO account_filters(name,description,group_masks,group_regex,login_min,login_max,"
      "manager_id,created_at,updated_at,user_predicate_json) VALUES(?,?,?,?,?,?,?,?,?,?)");
   st.BindText(1, f.name);
   st.BindText(2, f.description);
   st.BindText(3, Join(f.group_masks, ','));
   st.BindText(4, f.group_regex);
   if(f.login_min) st.BindU64(5, f.login_min); else st.BindNull(5);
   if(f.login_max) st.BindU64(6, f.login_max); else st.BindNull(6);
   if(f.manager_id) st.BindI64(7, f.manager_id); else st.BindNull(7);
   st.BindI64(8, now);
   st.BindI64(9, now);
   st.BindText(10, AccountFilterUserPredJson(f));
   st.Step();
   f.id = db.LastInsertRowid();
   f.created_at = f.updated_at = now;
   return f.id;
}

bool AccountFilterRepo::Update(SqliteDb& db, AccountFilter& f)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "UPDATE account_filters SET name=?,description=?,group_masks=?,group_regex=?,"
      "login_min=?,login_max=?,manager_id=?,updated_at=?,user_predicate_json=? WHERE id=?");
   st.BindText(1, f.name);
   st.BindText(2, f.description);
   st.BindText(3, Join(f.group_masks, ','));
   st.BindText(4, f.group_regex);
   if(f.login_min) st.BindU64(5, f.login_min); else st.BindNull(5);
   if(f.login_max) st.BindU64(6, f.login_max); else st.BindNull(6);
   if(f.manager_id) st.BindI64(7, f.manager_id); else st.BindNull(7);
   st.BindI64(8, now);
   st.BindText(9, AccountFilterUserPredJson(f));
   st.BindI64(10, f.id);
   st.Step();
   f.updated_at = now;
   return true;
}

bool AccountFilterRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM account_filters WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

//+--------------------- TemplateRepo ------------------------------+

namespace
{
   void FillTemplateFromStmt(SqliteStmt& st, ReportTemplate& t)
   {
      t.id            = st.ColI64(0);
      t.name          = st.ColText(1);
      t.description   = st.ColText(2);
      t.row_model     = st.ColText(3);
      t.date_params   = Expression::DateParamsFromJsonString(st.ColText(4));
      std::string err;
      Expression::ColumnsFromJsonString(st.ColText(5), &t.columns, &err);
      Expression::SortFromJsonString(st.ColText(6), &t.sort);
      t.default_top_n = (uint32_t)st.ColInt(7);
      t.created_at    = st.ColI64(8);
      t.updated_at    = st.ColI64(9);
   }
}

std::vector<ReportTemplate> TemplateRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<ReportTemplate> out;
   SqliteStmt st(db,
      "SELECT id,name,description,row_model,date_params,columns_json,sort_json,default_top_n,"
      "created_at,updated_at FROM report_templates ORDER BY id");
   while(st.Step())
   {
      ReportTemplate t; FillTemplateFromStmt(st, t);
      out.push_back(std::move(t));
   }
   return out;
}

std::optional<ReportTemplate> TemplateRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "SELECT id,name,description,row_model,date_params,columns_json,sort_json,default_top_n,"
      "created_at,updated_at FROM report_templates WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   ReportTemplate t; FillTemplateFromStmt(st, t);
   return t;
}

int64_t TemplateRepo::Insert(SqliteDb& db, ReportTemplate& t)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO report_templates(name,description,row_model,date_params,columns_json,"
      "sort_json,default_top_n,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?)");
   st.BindText(1, t.name);
   st.BindText(2, t.description);
   st.BindText(3, t.row_model.empty() ? "per_account" : t.row_model);
   st.BindText(4, Expression::DateParamsToJsonString(t.date_params));
   st.BindText(5, Expression::ColumnsToJsonString(t.columns));
   st.BindText(6, Expression::SortToJsonString(t.sort));
   st.BindInt (7, (int)t.default_top_n);
   st.BindI64 (8, now);
   st.BindI64 (9, now);
   st.Step();
   t.id = db.LastInsertRowid();
   t.created_at = t.updated_at = now;
   return t.id;
}

bool TemplateRepo::Update(SqliteDb& db, ReportTemplate& t)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "UPDATE report_templates SET name=?,description=?,row_model=?,date_params=?,"
      "columns_json=?,sort_json=?,default_top_n=?,updated_at=? WHERE id=?");
   st.BindText(1, t.name);
   st.BindText(2, t.description);
   st.BindText(3, t.row_model.empty() ? "per_account" : t.row_model);
   st.BindText(4, Expression::DateParamsToJsonString(t.date_params));
   st.BindText(5, Expression::ColumnsToJsonString(t.columns));
   st.BindText(6, Expression::SortToJsonString(t.sort));
   st.BindInt (7, (int)t.default_top_n);
   st.BindI64 (8, now);
   st.BindI64 (9, t.id);
   st.Step();
   t.updated_at = now;
   return true;
}

bool TemplateRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM report_templates WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

//+--------------------- BlueprintRepo -----------------------------+

namespace
{
   void FillBlueprintFromStmt(SqliteStmt& st, FormulaBlueprint& b)
   {
      b.id          = st.ColI64(0);
      b.name        = st.ColText(1);
      b.description = st.ColText(2);
      b.date_params = Expression::DateParamsFromJsonString(st.ColText(3));
      std::string err;
      nlohmann::json j = nlohmann::json::parse(st.ColText(4), nullptr, false);
      if(!j.is_discarded()) Expression::NodeFromJson(j, &b.expr, &err);
      b.created_at  = st.ColI64(5);
      b.updated_at  = st.ColI64(6);
   }
}

std::vector<FormulaBlueprint> BlueprintRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<FormulaBlueprint> out;
   SqliteStmt st(db,
      "SELECT id,name,description,date_params,expr_json,created_at,updated_at "
      "FROM formula_blueprints ORDER BY name");
   while(st.Step())
   {
      FormulaBlueprint b; FillBlueprintFromStmt(st, b);
      out.push_back(std::move(b));
   }
   return out;
}

std::optional<FormulaBlueprint> BlueprintRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "SELECT id,name,description,date_params,expr_json,created_at,updated_at "
      "FROM formula_blueprints WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   FormulaBlueprint b; FillBlueprintFromStmt(st, b);
   return b;
}

int64_t BlueprintRepo::Insert(SqliteDb& db, FormulaBlueprint& b)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   const std::string expr_json = b.expr ? Expression::NodeToJson(*b.expr).dump() : std::string("null");
   SqliteStmt st(db,
      "INSERT INTO formula_blueprints(name,description,date_params,expr_json,created_at,updated_at) "
      "VALUES(?,?,?,?,?,?)");
   st.BindText(1, b.name);
   st.BindText(2, b.description);
   st.BindText(3, Expression::DateParamsToJsonString(b.date_params));
   st.BindText(4, expr_json);
   st.BindI64 (5, now);
   st.BindI64 (6, now);
   st.Step();
   b.id = db.LastInsertRowid();
   b.created_at = b.updated_at = now;
   return b.id;
}

bool BlueprintRepo::Update(SqliteDb& db, FormulaBlueprint& b)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   const std::string expr_json = b.expr ? Expression::NodeToJson(*b.expr).dump() : std::string("null");
   SqliteStmt st(db,
      "UPDATE formula_blueprints SET name=?,description=?,date_params=?,expr_json=?,updated_at=? WHERE id=?");
   st.BindText(1, b.name);
   st.BindText(2, b.description);
   st.BindText(3, Expression::DateParamsToJsonString(b.date_params));
   st.BindText(4, expr_json);
   st.BindI64 (5, now);
   st.BindI64 (6, b.id);
   st.Step();
   b.updated_at = now;
   return true;
}

bool BlueprintRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM formula_blueprints WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

//+--------------------- JobRepo -----------------------------------+

namespace
{
   JobStatus ParseStatus(const std::string& s)
   {
      if(s == "running") return JobStatus::Running;
      if(s == "completed") return JobStatus::Completed;
      if(s == "failed") return JobStatus::Failed;
      return JobStatus::Queued;
   }

   void FillJobFromStmt(SqliteStmt& st, JobRow& j)
   {
      j.id                = st.ColI64(0);
      j.manager_id        = st.ColI64(1);
      j.template_id       = st.ColI64(2);
      j.account_filter_id = st.IsNull(3) ? 0 : st.ColI64(3);
      j.params_json       = st.ColText(4);
      j.status            = ParseStatus(st.ColText(5));
      j.progress          = st.ColReal(6);
      j.error_message     = st.ColText(7);
      j.output_dir        = st.ColText(8);
      j.csv_filename      = st.ColText(9);
      j.xlsx_filename     = st.ColText(10);
      j.summary_json      = st.ColText(11);
      j.created_at        = st.ColI64(12);
      j.started_at        = st.IsNull(13) ? 0 : st.ColI64(13);
      j.completed_at      = st.IsNull(14) ? 0 : st.ColI64(14);
   }
}

int64_t JobRepo::Create(SqliteDb& db, JobRow& j)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO report_jobs(manager_id,template_id,account_filter_id,params_json,status,progress,created_at) "
      "VALUES(?,?,?,?,?,?,?)");
   st.BindI64 (1, j.manager_id);
   st.BindI64 (2, j.template_id);
   if(j.account_filter_id) st.BindI64(3, j.account_filter_id); else st.BindNull(3);
   st.BindText(4, j.params_json);
   st.BindText(5, "queued");
   st.BindReal(6, 0.0);
   st.BindI64 (7, now);
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
      "SELECT id,manager_id,template_id,account_filter_id,params_json,status,progress,error_message,"
      "output_dir,csv_filename,xlsx_filename,summary_json,created_at,started_at,completed_at "
      "FROM report_jobs WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   JobRow j; FillJobFromStmt(st, j);
   return j;
}

std::vector<JobRow> JobRepo::List(SqliteDb& db, int limit)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<JobRow> out;
   SqliteStmt st(db,
      "SELECT id,manager_id,template_id,account_filter_id,params_json,status,progress,error_message,"
      "output_dir,csv_filename,xlsx_filename,summary_json,created_at,started_at,completed_at "
      "FROM report_jobs ORDER BY id DESC LIMIT ?");
   st.BindInt(1, limit);
   while(st.Step())
   {
      JobRow j; FillJobFromStmt(st, j);
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

//+--------------------- CacheRepo (not wired in v1) ---------------+

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

//+--------------------- ReadyMadeRepo ----------------------------+

namespace
{
   void FillReadyMadeFromStmt(SqliteStmt& st, ReadyMadeReport& r)
   {
      r.id                 = st.ColI64(0);
      r.name               = st.ColText(1);
      r.description        = st.ColText(2);
      r.template_id        = st.ColI64(3);
      r.account_filter_id  = st.IsNull(4) ? 0 : st.ColI64(4);
      r.date_strategy      = st.ColText(5);
      r.fixed_dates_json   = st.ColText(6);
      r.relative_preset    = st.ColText(7);
      r.relative_n         = st.ColInt(8);
      r.top_n_override     = (uint32_t)st.ColInt(9);
      r.created_at         = st.ColI64(10);
      r.updated_at         = st.ColI64(11);
   }
}

std::vector<ReadyMadeReport> ReadyMadeRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<ReadyMadeReport> out;
   SqliteStmt st(db,
      "SELECT id,name,description,template_id,account_filter_id,date_strategy,"
      "fixed_dates_json,relative_preset,relative_n,top_n_override,created_at,updated_at "
      "FROM ready_made_reports ORDER BY id");
   while(st.Step())
   {
      ReadyMadeReport r; FillReadyMadeFromStmt(st, r);
      out.push_back(std::move(r));
   }
   return out;
}

std::optional<ReadyMadeReport> ReadyMadeRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "SELECT id,name,description,template_id,account_filter_id,date_strategy,"
      "fixed_dates_json,relative_preset,relative_n,top_n_override,created_at,updated_at "
      "FROM ready_made_reports WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   ReadyMadeReport r; FillReadyMadeFromStmt(st, r);
   return r;
}

int64_t ReadyMadeRepo::Insert(SqliteDb& db, ReadyMadeReport& r)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO ready_made_reports(name,description,template_id,account_filter_id,"
      "date_strategy,fixed_dates_json,relative_preset,relative_n,top_n_override,"
      "created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
   st.BindText(1, r.name);
   st.BindText(2, r.description);
   st.BindI64 (3, r.template_id);
   if(r.account_filter_id) st.BindI64(4, r.account_filter_id); else st.BindNull(4);
   st.BindText(5, r.date_strategy.empty() ? "relative" : r.date_strategy);
   st.BindText(6, r.fixed_dates_json.empty() ? "{}" : r.fixed_dates_json);
   st.BindText(7, r.relative_preset.empty() ? "last_n_days" : r.relative_preset);
   st.BindInt (8, r.relative_n ? r.relative_n : 7);
   st.BindInt (9, (int)r.top_n_override);
   st.BindI64 (10, now);
   st.BindI64 (11, now);
   st.Step();
   r.id = db.LastInsertRowid();
   r.created_at = r.updated_at = now;
   return r.id;
}

bool ReadyMadeRepo::Update(SqliteDb& db, ReadyMadeReport& r)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "UPDATE ready_made_reports SET name=?,description=?,template_id=?,account_filter_id=?,"
      "date_strategy=?,fixed_dates_json=?,relative_preset=?,relative_n=?,top_n_override=?,"
      "updated_at=? WHERE id=?");
   st.BindText(1, r.name);
   st.BindText(2, r.description);
   st.BindI64 (3, r.template_id);
   if(r.account_filter_id) st.BindI64(4, r.account_filter_id); else st.BindNull(4);
   st.BindText(5, r.date_strategy.empty() ? "relative" : r.date_strategy);
   st.BindText(6, r.fixed_dates_json.empty() ? "{}" : r.fixed_dates_json);
   st.BindText(7, r.relative_preset.empty() ? "last_n_days" : r.relative_preset);
   st.BindInt (8, r.relative_n ? r.relative_n : 7);
   st.BindInt (9, (int)r.top_n_override);
   st.BindI64 (10, now);
   st.BindI64 (11, r.id);
   st.Step();
   r.updated_at = now;
   return true;
}

bool ReadyMadeRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM ready_made_reports WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

//+--------------------- ScheduleRepo -----------------------------+

namespace
{
   void FillScheduleFromStmt(SqliteStmt& st, ScheduleEntry& s)
   {
      s.id               = st.ColI64(0);
      s.name             = st.ColText(1);
      s.ready_made_id    = st.ColI64(2);
      s.frequency        = st.ColText(3);
      s.time_hour        = st.ColInt(4);
      s.time_minute      = st.ColInt(5);
      s.day_of_week      = st.ColInt(6);
      s.day_of_month     = st.ColInt(7);
      s.every_n_hours    = st.ColInt(8);
      s.telegram_chat_id = st.ColText(9);
      s.enabled          = st.ColInt(10) != 0;
      s.next_run_at      = st.ColI64(11);
      s.last_run_at      = st.ColI64(12);
      s.last_status      = st.ColText(13);
      s.last_job_id      = st.IsNull(14) ? 0 : st.ColI64(14);
      s.last_error       = st.ColText(15);
      s.created_at       = st.ColI64(16);
      s.updated_at       = st.ColI64(17);
   }

   const char* kScheduleSelectCols =
      "id,name,ready_made_id,frequency,time_hour,time_minute,day_of_week,day_of_month,"
      "every_n_hours,telegram_chat_id,enabled,next_run_at,last_run_at,last_status,"
      "last_job_id,last_error,created_at,updated_at";
}

std::vector<ScheduleEntry> ScheduleRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<ScheduleEntry> out;
   SqliteStmt st(db, std::string("SELECT ") + kScheduleSelectCols + " FROM schedules ORDER BY id");
   while(st.Step())
   {
      ScheduleEntry s; FillScheduleFromStmt(st, s);
      out.push_back(std::move(s));
   }
   return out;
}

std::optional<ScheduleEntry> ScheduleRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, std::string("SELECT ") + kScheduleSelectCols + " FROM schedules WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   ScheduleEntry s; FillScheduleFromStmt(st, s);
   return s;
}

int64_t ScheduleRepo::Insert(SqliteDb& db, ScheduleEntry& s)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO schedules(name,ready_made_id,frequency,time_hour,time_minute,"
      "day_of_week,day_of_month,every_n_hours,telegram_chat_id,enabled,next_run_at,"
      "last_run_at,last_status,last_error,created_at,updated_at) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
   st.BindText(1,  s.name);
   st.BindI64 (2,  s.ready_made_id);
   st.BindText(3,  s.frequency.empty() ? "daily" : s.frequency);
   st.BindInt (4,  s.time_hour);
   st.BindInt (5,  s.time_minute);
   st.BindInt (6,  s.day_of_week);
   st.BindInt (7,  s.day_of_month);
   st.BindInt (8,  s.every_n_hours);
   st.BindText(9,  s.telegram_chat_id);
   st.BindInt (10, s.enabled ? 1 : 0);
   st.BindI64 (11, s.next_run_at);
   st.BindI64 (12, s.last_run_at);
   st.BindText(13, s.last_status);
   st.BindText(14, s.last_error);
   st.BindI64 (15, now);
   st.BindI64 (16, now);
   st.Step();
   s.id = db.LastInsertRowid();
   s.created_at = s.updated_at = now;
   return s.id;
}

bool ScheduleRepo::Update(SqliteDb& db, ScheduleEntry& s)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "UPDATE schedules SET name=?,ready_made_id=?,frequency=?,time_hour=?,time_minute=?,"
      "day_of_week=?,day_of_month=?,every_n_hours=?,telegram_chat_id=?,enabled=?,"
      "next_run_at=?,updated_at=? WHERE id=?");
   st.BindText(1,  s.name);
   st.BindI64 (2,  s.ready_made_id);
   st.BindText(3,  s.frequency.empty() ? "daily" : s.frequency);
   st.BindInt (4,  s.time_hour);
   st.BindInt (5,  s.time_minute);
   st.BindInt (6,  s.day_of_week);
   st.BindInt (7,  s.day_of_month);
   st.BindInt (8,  s.every_n_hours);
   st.BindText(9,  s.telegram_chat_id);
   st.BindInt (10, s.enabled ? 1 : 0);
   st.BindI64 (11, s.next_run_at);
   st.BindI64 (12, now);
   st.BindI64 (13, s.id);
   st.Step();
   s.updated_at = now;
   return true;
}

bool ScheduleRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM schedules WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

std::vector<ScheduleEntry> ScheduleRepo::ListDue(SqliteDb& db, int64_t now)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<ScheduleEntry> out;
   SqliteStmt st(db, std::string("SELECT ") + kScheduleSelectCols +
                     " FROM schedules WHERE enabled=1 AND next_run_at <= ? "
                     "AND (last_status='' OR last_status='completed' OR last_status='failed')");
   st.BindI64(1, now);
   while(st.Step())
   {
      ScheduleEntry s; FillScheduleFromStmt(st, s);
      out.push_back(std::move(s));
   }
   return out;
}

std::vector<ScheduleEntry> ScheduleRepo::ListDispatched(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<ScheduleEntry> out;
   SqliteStmt st(db, std::string("SELECT ") + kScheduleSelectCols +
                     " FROM schedules WHERE last_status='dispatched'");
   while(st.Step())
   {
      ScheduleEntry s; FillScheduleFromStmt(st, s);
      out.push_back(std::move(s));
   }
   return out;
}

bool ScheduleRepo::UpdateDispatch(SqliteDb& db, int64_t id, int64_t last_run_at,
                                  int64_t next_run_at, int64_t last_job_id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "UPDATE schedules SET last_run_at=?,next_run_at=?,last_status='dispatched',"
      "last_job_id=?,last_error='' WHERE id=?");
   st.BindI64(1, last_run_at);
   st.BindI64(2, next_run_at);
   st.BindI64(3, last_job_id);
   st.BindI64(4, id);
   st.Step();
   return true;
}

bool ScheduleRepo::UpdateDelivery(SqliteDb& db, int64_t id, const std::string& status,
                                  const std::string& last_error)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "UPDATE schedules SET last_status=?, last_error=? WHERE id=?");
   st.BindText(1, status);
   st.BindText(2, last_error);
   st.BindI64 (3, id);
   st.Step();
   return true;
}

//+--------------------- SettingsRepo -----------------------------+

std::string SettingsRepo::Get(SqliteDb& db, const std::string& key)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "SELECT value FROM app_settings WHERE key=?");
   st.BindText(1, key);
   if(!st.Step()) return "";
   return st.ColText(0);
}

void SettingsRepo::Set(SqliteDb& db, const std::string& key, const std::string& value)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "INSERT INTO app_settings(key,value) VALUES(?,?) "
                     "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
   st.BindText(1, key);
   st.BindText(2, value);
   st.Step();
}

//+--------------------- UserRepo ----------------------------------+

namespace
{
   const char* kUserCols =
      "id,username,password_hash,role,active,created_at,updated_at,last_login_at";

   void FillUser(SqliteStmt& st, User& u)
   {
      u.id            = st.ColI64(0);
      u.username      = st.ColText(1);
      u.password_hash = st.ColText(2);
      u.role          = st.ColText(3);
      u.active        = st.ColInt(4) != 0;
      u.created_at    = st.ColI64(5);
      u.updated_at    = st.ColI64(6);
      u.last_login_at = st.ColI64(7);
   }
}

std::vector<User> UserRepo::ListAll(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   std::vector<User> out;
   SqliteStmt st(db, std::string("SELECT ") + kUserCols + " FROM users ORDER BY id");
   while(st.Step()) { User u; FillUser(st, u); out.push_back(std::move(u)); }
   return out;
}

std::optional<User> UserRepo::Get(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, std::string("SELECT ") + kUserCols + " FROM users WHERE id=?");
   st.BindI64(1, id);
   if(!st.Step()) return std::nullopt;
   User u; FillUser(st, u);
   return u;
}

std::optional<User> UserRepo::GetByUsername(SqliteDb& db, const std::string& username)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, std::string("SELECT ") + kUserCols + " FROM users WHERE username=?");
   st.BindText(1, username);
   if(!st.Step()) return std::nullopt;
   User u; FillUser(st, u);
   return u;
}

int64_t UserRepo::Insert(SqliteDb& db, User& u)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db,
      "INSERT INTO users(username,password_hash,role,active,created_at,updated_at,last_login_at) "
      "VALUES(?,?,?,?,?,?,0)");
   st.BindText(1, u.username);
   st.BindText(2, u.password_hash);
   st.BindText(3, u.role.empty() ? "viewer" : u.role);
   st.BindInt (4, u.active ? 1 : 0);
   st.BindI64 (5, now);
   st.BindI64 (6, now);
   st.Step();
   u.id = db.LastInsertRowid();
   u.created_at = u.updated_at = now;
   return u.id;
}

bool UserRepo::UpdateRoleActive(SqliteDb& db, int64_t id, const std::string& role, bool active)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db, "UPDATE users SET role=?, active=?, updated_at=? WHERE id=?");
   st.BindText(1, role);
   st.BindInt (2, active ? 1 : 0);
   st.BindI64 (3, now);
   st.BindI64 (4, id);
   st.Step();
   return true;
}

bool UserRepo::UpdatePassword(SqliteDb& db, int64_t id, const std::string& password_hash)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   const int64_t now = (int64_t)time(nullptr);
   SqliteStmt st(db, "UPDATE users SET password_hash=?, updated_at=? WHERE id=?");
   st.BindText(1, password_hash);
   st.BindI64 (2, now);
   st.BindI64 (3, id);
   st.Step();
   return true;
}

bool UserRepo::UpdateLastLogin(SqliteDb& db, int64_t id, int64_t when)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "UPDATE users SET last_login_at=? WHERE id=?");
   st.BindI64(1, when);
   st.BindI64(2, id);
   st.Step();
   return true;
}

bool UserRepo::Delete(SqliteDb& db, int64_t id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM users WHERE id=?");
   st.BindI64(1, id);
   st.Step();
   return true;
}

int64_t UserRepo::Count(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "SELECT COUNT(*) FROM users");
   if(!st.Step()) return 0;
   return st.ColI64(0);
}

int64_t UserRepo::CountActiveAdmins(SqliteDb& db)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "SELECT COUNT(*) FROM users WHERE role='admin' AND active=1");
   if(!st.Step()) return 0;
   return st.ColI64(0);
}

//+--------------------- SessionRepo -------------------------------+

namespace
{
   const char* kSessionCols =
      "token,user_id,created_at,expires_at,remote_addr,user_agent";

   void FillSession(SqliteStmt& st, Session& s)
   {
      s.token       = st.ColText(0);
      s.user_id     = st.ColI64(1);
      s.created_at  = st.ColI64(2);
      s.expires_at  = st.ColI64(3);
      s.remote_addr = st.ColText(4);
      s.user_agent  = st.ColText(5);
   }
}

bool SessionRepo::Insert(SqliteDb& db, const Session& s)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db,
      "INSERT INTO sessions(token,user_id,created_at,expires_at,remote_addr,user_agent) "
      "VALUES(?,?,?,?,?,?)");
   st.BindText(1, s.token);
   st.BindI64 (2, s.user_id);
   st.BindI64 (3, s.created_at);
   st.BindI64 (4, s.expires_at);
   st.BindText(5, s.remote_addr);
   st.BindText(6, s.user_agent);
   st.Step();
   return true;
}

std::optional<Session> SessionRepo::Get(SqliteDb& db, const std::string& token)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, std::string("SELECT ") + kSessionCols + " FROM sessions WHERE token=?");
   st.BindText(1, token);
   if(!st.Step()) return std::nullopt;
   Session s; FillSession(st, s);
   return s;
}

bool SessionRepo::Touch(SqliteDb& db, const std::string& token, int64_t new_expires_at)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "UPDATE sessions SET expires_at=? WHERE token=?");
   st.BindI64 (1, new_expires_at);
   st.BindText(2, token);
   st.Step();
   return true;
}

bool SessionRepo::Delete(SqliteDb& db, const std::string& token)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM sessions WHERE token=?");
   st.BindText(1, token);
   st.Step();
   return true;
}

int64_t SessionRepo::DeleteByUser(SqliteDb& db, int64_t user_id)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM sessions WHERE user_id=?");
   st.BindI64(1, user_id);
   st.Step();
   return (int64_t)sqlite3_changes(db.Handle());
}

int64_t SessionRepo::DeleteByUserExcept(SqliteDb& db, int64_t user_id, const std::string& keep_token)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM sessions WHERE user_id=? AND token<>?");
   st.BindI64 (1, user_id);
   st.BindText(2, keep_token);
   st.Step();
   return (int64_t)sqlite3_changes(db.Handle());
}

int64_t SessionRepo::DeleteExpired(SqliteDb& db, int64_t now)
{
   std::lock_guard<std::mutex> lock(db.Mutex());
   SqliteStmt st(db, "DELETE FROM sessions WHERE expires_at <= ?");
   st.BindI64(1, now);
   st.Step();
   return (int64_t)sqlite3_changes(db.Handle());
}
