//+------------------------------------------------------------------+
//|                                              MT5 ReportTool      |
//|              Records.h - POD types shared across modules         |
//+------------------------------------------------------------------+
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <limits>
#include <variant>
#include <memory>

//--- Static user profile (IMTUser). Populated from UserRequestArray /
//--- UserRequestByLogins. All wide-strings are UTF-8 on this side.
struct UserInfo
{
   uint64_t    login                    = 0;
   std::string group;
   std::string currency;                // not actually on IMTUser; derived from IMTDaily/IMTAccount when paired
   std::string name;
   std::string first_name;
   std::string last_name;
   std::string middle_name;
   std::string email;
   std::string phone;
   std::string country;
   std::string state;
   std::string city;
   std::string zip_code;
   std::string address;
   std::string id;
   std::string company;
   std::string account_tag;              // broker-side account
   std::string status;
   std::string comment;
   std::string lead_campaign;
   std::string lead_source;
   std::string last_ip;
   uint64_t    agent                    = 0;
   uint64_t    client_id                = 0;
   uint32_t    leverage                 = 0;
   uint32_t    language                 = 0;
   uint32_t    color                    = 0;
   uint32_t    limit_orders             = 0;
   uint64_t    rights                   = 0;
   int64_t     registration             = 0;
   int64_t     last_access              = 0;
   int64_t     last_pass_change         = 0;
   double      balance                  = 0.0;
   double      credit                   = 0.0;
   double      interest_rate            = 0.0;
   double      limit_positions_value    = 0.0;
   double      commission_daily         = 0.0;
   double      commission_monthly       = 0.0;
   double      commission_agent_daily   = 0.0;
   double      commission_agent_monthly = 0.0;
   double      balance_prev_day         = 0.0;
   double      balance_prev_month       = 0.0;
   double      equity_prev_day          = 0.0;
   double      equity_prev_month        = 0.0;
};

//--- Live account snapshot (IMTAccount). One row per login at fetch time.
struct AccountInfo
{
   uint64_t login              = 0;
   uint32_t currency_digits    = 2;
   double   balance            = 0.0;
   double   credit             = 0.0;
   double   margin             = 0.0;
   double   margin_free        = 0.0;
   double   margin_level       = 0.0;
   uint32_t margin_leverage    = 0;
   double   margin_initial     = 0.0;
   double   margin_maintenance = 0.0;
   double   profit             = 0.0;
   double   storage            = 0.0;
   double   floating           = 0.0;
   double   equity             = 0.0;
   double   assets             = 0.0;
   double   liabilities        = 0.0;
   double   blocked_commission = 0.0;
   double   blocked_profit     = 0.0;
   uint32_t so_activation      = 0;
   int64_t  so_time            = 0;
   double   so_level           = 0.0;
   double   so_equity          = 0.0;
   double   so_margin          = 0.0;
};

//--- Closed deal (IMTDeal). Comprehensive field set for FieldCatalog F category.
struct DealRow
{
   uint64_t    ticket             = 0;
   uint64_t    login              = 0;
   uint64_t    order              = 0;
   uint64_t    dealer             = 0;
   uint64_t    position_id        = 0;
   uint64_t    expert_id          = 0;
   uint32_t    action             = 0;
   uint32_t    entry              = 0;
   uint32_t    reason             = 0;
   uint32_t    digits             = 0;
   uint32_t    digits_currency    = 2;
   uint64_t    flags              = 0;
   uint32_t    modification_flags = 0;
   int64_t     time               = 0;
   int64_t     time_msc           = 0;
   double      contract_size      = 0.0;
   double      profit             = 0.0;
   double      profit_raw         = 0.0;
   double      storage            = 0.0;
   double      commission         = 0.0;
   double      fee                = 0.0;
   double      value              = 0.0;
   uint64_t    volume             = 0;
   uint64_t    volume_ext         = 0;
   uint64_t    volume_closed      = 0;
   uint64_t    volume_closed_ext  = 0;
   double      price              = 0.0;
   double      price_position     = 0.0;
   double      price_sl           = 0.0;
   double      price_tp           = 0.0;
   double      price_gateway      = 0.0;
   double      market_bid         = 0.0;
   double      market_ask         = 0.0;
   double      market_last        = 0.0;
   double      tick_value         = 0.0;
   double      tick_size          = 0.0;
   double      rate_profit        = 0.0;
   double      rate_margin        = 0.0;
   std::string symbol;
   std::string comment;
   std::string gateway;
   std::string external_id;

   //--- Derived (not from MT5 SDK). Filled by Engine after load via
   //--- position_id pairing: for OUT/OUT_BY deals, seconds since the
   //--- matching IN deal in the same fetch window. 0 when no IN match.
   int64_t     trade_lifetime_sec = 0;
};

//--- EOD daily snapshot (IMTDaily, heavy variant).
struct DailyRow
{
   uint64_t    login                = 0;
   int64_t     datetime             = 0;
   int64_t     datetime_prev        = 0;
   double      balance              = 0.0;
   double      credit               = 0.0;
   double      profit_equity        = 0.0;   // EOD equity
   double      margin               = 0.0;
   double      margin_free          = 0.0;
   double      margin_level         = 0.0;
   uint32_t    margin_leverage      = 0;
   double      profit               = 0.0;   // floating
   double      profit_storage       = 0.0;
   double      profit_assets        = 0.0;
   double      profit_liabilities   = 0.0;
   double      interest_rate        = 0.0;
   double      commission_daily     = 0.0;
   double      commission_monthly   = 0.0;
   double      agent_daily          = 0.0;
   double      agent_monthly        = 0.0;
   double      balance_prev_day     = 0.0;
   double      balance_prev_month   = 0.0;
   double      equity_prev_day      = 0.0;
   double      equity_prev_month    = 0.0;
   double      daily_profit         = 0.0;
   double      daily_balance        = 0.0;
   double      daily_credit         = 0.0;
   double      daily_charge         = 0.0;
   double      daily_correction     = 0.0;
   double      daily_bonus          = 0.0;
   double      daily_storage        = 0.0;
   double      daily_comm_instant   = 0.0;
   double      daily_comm_round     = 0.0;
   double      daily_agent          = 0.0;
   double      daily_interest       = 0.0;
   std::string currency;
   uint32_t    currency_digits      = 2;
};

//--- Open position snapshot (IMTPosition).
struct PositionRow
{
   uint64_t    login            = 0;
   uint32_t    action           = 0;   // BUY/SELL
   uint32_t    digits           = 0;
   uint32_t    digits_currency  = 2;
   double      contract_size    = 0.0;
   int64_t     time_create      = 0;
   int64_t     time_update      = 0;
   double      price_open       = 0.0;
   double      price_current    = 0.0;
   double      price_sl         = 0.0;
   double      price_tp         = 0.0;
   uint64_t    volume           = 0;
   double      profit           = 0.0;
   double      storage          = 0.0;
   double      rate_profit      = 0.0;
   double      rate_margin      = 0.0;
   uint64_t    expert_id        = 0;
   uint64_t    expert_position_id = 0;
   uint32_t    activation_mode  = 0;
   int64_t     activation_time  = 0;
   double      activation_price = 0.0;
   uint32_t    activation_flags = 0;
   std::string symbol;
   std::string comment;
};

//--- Open or historical order (IMTOrder). Same fields cover both use cases;
//--- HistoryOrderRow is an alias for clarity in call sites.
struct OrderRow
{
   uint64_t    order             = 0;   // ticket
   uint64_t    login             = 0;
   uint64_t    dealer            = 0;
   uint64_t    expert_id         = 0;
   uint64_t    position_id       = 0;
   uint32_t    digits            = 0;
   uint32_t    digits_currency   = 2;
   double      contract_size     = 0.0;
   uint32_t    state             = 0;
   uint32_t    reason            = 0;
   uint32_t    type              = 0;
   uint32_t    type_fill         = 0;
   uint32_t    type_time         = 0;
   int64_t     time_setup        = 0;
   int64_t     time_setup_msc    = 0;
   int64_t     time_expiration   = 0;
   int64_t     time_done         = 0;
   int64_t     time_done_msc     = 0;
   double      price_order       = 0.0;
   double      price_trigger     = 0.0;
   double      price_current     = 0.0;
   double      price_sl          = 0.0;
   double      price_tp          = 0.0;
   uint64_t    volume_initial    = 0;
   uint64_t    volume_current    = 0;
   double      rate_margin       = 0.0;
   uint32_t    activation_mode   = 0;
   int64_t     activation_time   = 0;
   double      activation_price  = 0.0;
   uint32_t    activation_flags  = 0;
   std::string symbol;
   std::string comment;
   std::string external_id;
};

using OpenOrderRow    = OrderRow;
using HistoryOrderRow = OrderRow;

//--- ReportTool-specific types ---

struct RegexFilters
{
   std::vector<std::string> deposit;
   std::vector<std::string> withdrawal;
   std::vector<std::string> writeoff;
   std::vector<std::string> adjustment;
};

struct ManagerRow
{
   int64_t      id           = 0;
   std::string  name;
   std::string  brand;
   std::string  region;
   std::string  server;       // host:port
   uint64_t     manager_login = 0;
   std::string  password;     // plaintext after decrypt
   std::vector<std::string> group_masks;   // MT5 wildcard list
   std::string  group_regex;                // optional ECMAScript post-filter
   uint64_t     login_min     = 0;          // 0 = unset
   uint64_t     login_max     = 0;          // 0 = unset
   bool         active        = true;
   int64_t      created_at    = 0;
   int64_t      updated_at    = 0;
   RegexFilters regex_filters;
};

//--- Saved account filter preset (account_filters table).
struct Predicate;   // forward-declare (full definition appears below)
struct AccountFilter
{
   int64_t                   id          = 0;
   std::string               name;
   std::string               description;
   std::vector<std::string>  group_masks;
   std::string               group_regex;
   uint64_t                  login_min   = 0;
   uint64_t                  login_max   = 0;
   int64_t                   manager_id  = 0;     // 0 = unbound (generic, reusable across managers)
   //--- Optional client-side post-filter applied to each fetched user row
   //--- (covers IMTUser fields like comment, agent, zip, country, …).
   std::shared_ptr<Predicate> user_predicate;
   int64_t                   folder_id   = 0;     // 0 = NULL = unfiled (FK -> folders.id)
   int                       sort_order  = 0;     // v13: rank among siblings (folder + entity intermix)
   int64_t                   created_at  = 0;
   int64_t                   updated_at  = 0;
};

//--- Per-aggregator predicate over the underlying row (deal/daily/position/order).
enum class FilterOp
{
   Eq, Neq, Lt, Lte, Gt, Gte,
   Regex, Contains, StartsWith, EndsWith, In, Glob
};

inline const char* FilterOpName(FilterOp op)
{
   switch(op)
   {
      case FilterOp::Eq:         return "eq";
      case FilterOp::Neq:        return "neq";
      case FilterOp::Lt:         return "lt";
      case FilterOp::Lte:        return "lte";
      case FilterOp::Gt:         return "gt";
      case FilterOp::Gte:        return "gte";
      case FilterOp::Regex:      return "regex";
      case FilterOp::Contains:   return "contains";
      case FilterOp::StartsWith: return "startswith";
      case FilterOp::EndsWith:   return "endswith";
      case FilterOp::In:         return "in";
      case FilterOp::Glob:       return "glob";
   }
   return "?";
}

inline bool FilterOpFromName(const std::string& s, FilterOp* out)
{
   if(s == "eq")         { *out = FilterOp::Eq;         return true; }
   if(s == "neq")        { *out = FilterOp::Neq;        return true; }
   if(s == "lt")         { *out = FilterOp::Lt;         return true; }
   if(s == "lte")        { *out = FilterOp::Lte;        return true; }
   if(s == "gt")         { *out = FilterOp::Gt;         return true; }
   if(s == "gte")        { *out = FilterOp::Gte;        return true; }
   if(s == "regex")      { *out = FilterOp::Regex;      return true; }
   if(s == "contains")   { *out = FilterOp::Contains;   return true; }
   if(s == "startswith") { *out = FilterOp::StartsWith; return true; }
   if(s == "endswith")   { *out = FilterOp::EndsWith;   return true; }
   if(s == "in")         { *out = FilterOp::In;         return true; }
   if(s == "glob")       { *out = FilterOp::Glob;       return true; }
   return false;
}

//--- Leaf comparison: field <op> value. value is text or numeric (or list for 'in').
struct FieldFilter
{
   std::string              field;
   FilterOp                 op         = FilterOp::Eq;
   bool                     is_numeric = false;
   double                   value_num  = 0.0;
   std::string              value_str;
   std::vector<std::string> value_list;        // for `in`
   std::vector<double>      value_list_num;    // for `in` over numeric fields
};

//--- Boolean predicate tree over a row.
struct Predicate
{
   enum class Kind { Cmp, And, Or, Not };

   Kind kind = Kind::Cmp;

   FieldFilter cmp;                                           // Kind::Cmp
   std::vector<std::shared_ptr<Predicate>> children;          // Kind::And | Or
   std::shared_ptr<Predicate>              child;             // Kind::Not
};

//--- Expression AST node. JSON-serialized; recursive via shared_ptr children.
struct ExprNode
{
   enum class Type { Field, BinOp, Literal, ColRef };

   Type        type = Type::Literal;

   //--- Field
   std::string field_name;
   std::vector<std::string> field_args;   // date_param names; arity defined by FieldCatalog
   std::shared_ptr<Predicate> predicate;  // optional per-row filter for aggregator fields

   //--- BinOp
   char        op = '+';                   // '+' '-' '*' '/'
   std::shared_ptr<ExprNode> left;
   std::shared_ptr<ExprNode> right;

   //--- Literal
   double      literal_value = 0.0;

   //--- ColRef — reference to another column in the same template, by key.
   //--- Engine resolves via EvalContext::column_values populated left-to-right.
   std::string col_ref_key;
};

//--- One column in a template.
struct ColumnSpec
{
   enum class Kind   { Identifier, Formula };
   enum class Format { Money, Pct, Int, Text, Date };

   std::string key;     // stable id used by sort_json
   std::string label;   // header text
   Kind        kind   = Kind::Formula;
   Format      format = Format::Money;

   //--- Identifier
   std::string source;  // e.g. "login", "group", "name", "user_balance"

   //--- Formula
   std::shared_ptr<ExprNode> expr;

   //--- Composite pivot flag. When true on an identifier column, that column
   //--- contributes to the row bucket key (engine groups by the tuple of all
   //--- pivot_key columns in column order). When false, the identifier renders
   //--- a display value pulled from the bucket's representative record.
   //--- Backward compat: legacy templates lack this field; on parse, the FIRST
   //--- identifier column is implicitly pivot_key=true so single-pivot
   //--- templates keep their existing behaviour.
   bool pivot_key = false;

   //--- Per-pivot-column row filter. Consulted on every column with
   //--- pivot_key=true. The predicate's source must match the column's source:
   //--- user-source identifiers → UserInfo predicate; symbol/ticket → DealRow.
   std::shared_ptr<Predicate> row_predicate;
};

struct SortSpec
{
   std::string column_key;
   bool        descending = true;
   //--- When true and the sort column resolves to a numeric cell, rows are
   //--- ordered by |x| instead of x. Combined with `descending`: desc+abs
   //--- = biggest magnitude on top (big winners + losers cluster); asc+abs
   //--- = rows nearest zero on top. Ignored for text cells.
   bool        abs_value  = false;
};

//--- Saved formula building block (formula_blueprints table).
struct FormulaBlueprint
{
   int64_t                   id          = 0;
   std::string               name;
   std::string               description;
   std::vector<std::string>  date_params;       // param slot names used inside expr
   std::shared_ptr<ExprNode> expr;
   int64_t                   folder_id   = 0;   // 0 = NULL = unfiled (FK -> folders.id)
   int                       sort_order  = 0;   // v13: rank among siblings
   int64_t                   created_at  = 0;
   int64_t                   updated_at  = 0;
};

struct ReportTemplate
{
   int64_t                  id            = 0;
   std::string              name;
   std::string              description;
   std::string              row_model    = "per_account";
   std::vector<std::string> date_params;            // ordered named slots
   std::vector<ColumnSpec>  columns;
   SortSpec                 sort;
   uint32_t                 default_top_n = 0;   // 0 = no limit
   int64_t                  folder_id     = 0;   // 0 = NULL = unfiled (FK -> folders.id)
   int                      sort_order    = 0;   // v13: rank among siblings
   int64_t                  created_at    = 0;
   int64_t                  updated_at    = 0;
   int64_t                  deleted_at    = 0;   // 0 = live; >0 = soft-deleted at that UTC second
};

//--- Job tracking ---------------------------------------------------

enum class JobStatus { Queued, Running, Completed, Failed };

inline const char* JobStatusName(JobStatus s)
{
   switch(s)
   {
      case JobStatus::Queued:    return "queued";
      case JobStatus::Running:   return "running";
      case JobStatus::Completed: return "completed";
      case JobStatus::Failed:    return "failed";
   }
   return "?";
}

//--- Saved bundle: template + filter + date strategy (ready_made_reports table)
struct ReadyMadeReport
{
   int64_t      id                 = 0;
   std::string  name;
   std::string  description;
   int64_t      template_id        = 0;
   int64_t      account_filter_id  = 0;     // 0 = none

   std::string  date_strategy;              // "fixed" | "relative"
   std::string  fixed_dates_json;            // JSON object: { "date_from":"YYYY-MM-DD", ... }
   std::string  relative_preset;             // "today"|"yesterday"|"last_n_days"|"this_week"|"last_week"|"this_month"|"last_month"
   int          relative_n         = 7;      // used when preset = last_n_days

   uint32_t     top_n_override     = 0;     // 0 = use template default

   int64_t      folder_id          = 0;     // 0 = NULL = unfiled (FK -> folders.id)
   int          sort_order         = 0;     // v13: rank among siblings
   int64_t      created_at         = 0;
   int64_t      updated_at         = 0;
};

//--- Background scheduler entry (schedules table)
struct ScheduleEntry
{
   int64_t      id                  = 0;
   std::string  name;
   int64_t      ready_made_id       = 0;

   std::string  frequency;                  // "daily" | "weekly" | "monthly" | "hourly"
   int          time_hour           = 8;
   int          time_minute         = 0;
   int          day_of_week         = 1;    // 0=Sun..6=Sat (weekly)
   int          day_of_month        = 1;    // 1-28 (monthly)
   int          every_n_hours       = 1;    // hourly legacy fallback
   //--- v10 — richer recurrence. `hours`: subset of 0..23 (hourly only;
   //--- empty falls back to every_n_hours). `days_of_week`: subset of 0..6
   //--- with 0=Sun (used by daily + hourly; empty means every day).
   std::vector<int> hours;
   std::vector<int> days_of_week;

   std::string  telegram_chat_id;           // empty → fallback to global default
   std::string  delivery_format     = "csv"; // "csv" (SendDocument) | "text" (SendMessage summary)
   bool         enabled             = true;
   int64_t      folder_id           = 0;    // 0 = NULL = unfiled (FK -> folders.id)
   int          sort_order          = 0;    // v13: rank among siblings

   int64_t      next_run_at         = 0;    // UTC unix
   int64_t      last_run_at         = 0;
   std::string  last_status;                // "" | "dispatched" | "completed" | "failed"
   int64_t      last_job_id         = 0;
   std::string  last_error;

   int64_t      created_at          = 0;
   int64_t      updated_at          = 0;
};

//--- User-defined organisational folder over a single entity type. Linked to
//--- entity rows via the entity table's `folder_id` (SET NULL on delete).
//--- `item_count` isn't a column — it's populated by FolderRepo::ListByEntity.
struct FolderRow
{
   int64_t     id          = 0;
   std::string entity_type;       // "template"|"schedule"|"blueprint"|"ready_made"|"account_filter"
   std::string name;
   int         sort_order  = 0;
   int64_t     parent_id   = 0;   // 0 = top-level; > 0 = nested under that folder (same entity_type)
   int64_t     item_count  = 0;
   int64_t     created_at  = 0;
   int64_t     updated_at  = 0;
};

//--- Key/value app settings (app_settings table)
struct AppSetting
{
   std::string key;
   std::string value;
};

//--- Application user with role (users table).
//--- password_hash is PBKDF2-HMAC-SHA256 "pbkdf2$<iter>$<salt_b64>$<hash_b64>".
struct User
{
   int64_t      id            = 0;
   std::string  username;
   std::string  password_hash;
   std::string  role;                       // "admin" | "viewer"
   bool         active        = true;
   int64_t      created_at    = 0;
   int64_t      updated_at    = 0;
   int64_t      last_login_at = 0;
};

//--- Active HTTP session bound to a user (sessions table).
struct Session
{
   std::string  token;
   int64_t      user_id     = 0;
   int64_t      created_at  = 0;
   int64_t      expires_at  = 0;
   std::string  remote_addr;
   std::string  user_agent;
};

struct JobRow
{
   int64_t      id                 = 0;
   int64_t      manager_id         = 0;
   int64_t      template_id        = 0;
   int64_t      account_filter_id  = 0;     // 0 = no override / use manager's filter
   std::string  params_json;
   JobStatus    status             = JobStatus::Queued;
   double       progress           = 0.0;
   std::string  error_message;
   std::string  output_dir;
   std::string  csv_filename;
   std::string  xlsx_filename;
   std::string  summary_json;
   int64_t      created_at         = 0;
   int64_t      started_at         = 0;
   int64_t      completed_at       = 0;
};
