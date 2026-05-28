//--- Auth & users ------------------------------------------------

export type UserRole = 'admin' | 'viewer';

export type AppUser = {
  id:            number;
  username:      string;
  role:          UserRole;
  active:        boolean;
  created_at:    number;
  updated_at:    number;
  //--- Last time the user made an authenticated request (throttled to once
   //--- per minute in the server-side middleware). DB column is still named
   //--- `last_login_at` for legacy reasons but the value tracks activity.
  last_active_at: number;
};

export type LoginRequest = { username: string; password: string };
export type SetupRequest = { username: string; password: string };
export type SetupStatus  = { needs_setup: boolean };

export type UserCreateInput = { username: string; password: string; role: UserRole; active?: boolean };
export type UserPatchInput  = { role?: UserRole; active?: boolean };

export type RegexFilters = {
  deposit:    string[];
  withdrawal: string[];
  writeoff:   string[];
  adjustment: string[];
};

export type Manager = {
  id: number;
  name: string;
  brand: string;
  region: string;
  server: string;
  manager_login: number;
  group_masks: string[];
  group_regex: string;
  login_min: number | null;
  login_max: number | null;
  active: boolean;
  created_at: number;
  updated_at: number;
  regex_filters: RegexFilters;
};

export type ManagerInput = Omit<Manager, 'id' | 'created_at' | 'updated_at'> & {
  password?: string;
};

//--- Saved filter preset (account_filters row) -------------------

export type AccountFilter = {
  id: number;
  name: string;
  description: string;
  group_masks: string[];
  group_regex: string;
  login_min: number | null;
  login_max: number | null;
  manager_id: number | null;
  user_predicate?: Predicate | null;
  folder_id?: number | null;
  sort_order?: number;
  created_at: number;
  updated_at: number;
};

export type AccountFilterInput = Omit<AccountFilter, 'id' | 'created_at' | 'updated_at'>;

//--- DepositFilter — broker-specific cash-flow classifier with four fixed
//--- predicate slots. The template references the hardcoded aggregator
//--- fields (sum_cash_deposit, count_promotion, …); ready-made reports
//--- bind to a DepositFilter so the engine picks each predicate at run
//--- time. Result: one template runs across every broker — only the
//--- predicates change. Any slot may be null when the broker doesn't
//--- distinguish that category; the matching field then returns 0.
export type DepositFilter = {
  id: number;
  name: string;
  description: string;
  cash_deposit:    Predicate | null;
  cash_withdrawal: Predicate | null;
  promotion:       Predicate | null;
  rebate:          Predicate | null;
  sort_order?: number;
  created_at: number;
  updated_at: number;
};
export type DepositFilterInput = Omit<DepositFilter, 'id' | 'created_at' | 'updated_at'>;
//--- The four canonical bucket keys — kept in sync with the backend's
//--- kBucketKeys and the eight K-category aggregator fields.
export const DEPOSIT_BUCKET_KEYS = ['cash_deposit', 'cash_withdrawal', 'promotion', 'rebate'] as const;
export type DepositBucketKey = typeof DEPOSIT_BUCKET_KEYS[number];
export const DEPOSIT_BUCKET_LABELS: Record<DepositBucketKey, string> = {
  cash_deposit:    'Cash deposit',
  cash_withdrawal: 'Cash withdrawal',
  promotion:       'Promotion',
  rebate:          'Rebate',
};

//--- Organisational folders shared across the five user-content entities.

export type FolderEntityType =
  | 'template' | 'schedule' | 'blueprint' | 'ready_made' | 'account_filter';

export type Folder = {
  id: number;
  entity_type: FolderEntityType;
  name: string;
  sort_order: number;
  //--- Folder hierarchy (v11). null = top-level. Children share the same
  //--- entity_type as their parent. Deleting a parent promotes its children
  //--- to top-level (ON DELETE SET NULL).
  parent_id: number | null;
  item_count: number;
  created_at: number;
  updated_at: number;
};

//--- Predicate (per-row filter for aggregator fields) ------------

export type FilterOp =
  | 'eq' | 'neq' | 'lt' | 'lte' | 'gt' | 'gte'
  | 'regex' | 'glob' | 'contains' | 'startswith' | 'endswith' | 'in';

export type PredCmp = {
  kind: 'cmp';
  field: string;
  op:    FilterOp;
  value: number | string | string[] | number[];
};
export type PredAnd = { kind: 'and'; items: Predicate[] };
export type PredOr  = { kind: 'or';  items: Predicate[] };
export type PredNot = { kind: 'not'; item: Predicate };
export type Predicate = PredCmp | PredAnd | PredOr | PredNot;

//--- Expression AST ----------------------------------------------

export type ExprLiteral = { type: 'literal'; value: number };
export type ExprField   = { type: 'field';   name: string; args: string[]; predicate?: Predicate };
export type ExprBinOp   = { type: 'binop';   op: '+' | '-' | '*' | '/'; left: ExprNode; right: ExprNode };
export type ExprColRef  = { type: 'col_ref'; key: string };
export type ExprNode    = ExprLiteral | ExprField | ExprBinOp | ExprColRef;

//--- Field catalog (returned by GET /api/reports/fields) ---------

export type FieldSource =
  | 'user'
  | 'account'
  | 'daily'
  | 'deal'
  | 'position'
  | 'order_open'
  | 'order_hist'
  | 'literal';

export type FieldArity = 0 | 1 | 2;
export type FieldReturn = 'money' | 'int' | 'pct' | 'text' | 'date';

export type FieldDef = {
  name:               string;
  label:              string;
  category:           string;          // display group (A..K)
  source:             FieldSource;
  arity:              FieldArity;
  return_type:        FieldReturn;
  is_identifier:      boolean;
  supports_predicate: boolean;
  description?:       string;
};

export type FilterValueType = 'num' | 'text' | 'enum';
export type FilterableField = {
  name:        string;
  label:       string;
  type:        FilterValueType;
  enum_values?: { code: number; label: string }[];
};
export type FilterableBySource = Partial<Record<FieldSource, FilterableField[]>>;

export type FieldCatalog = {
  categories: { id: string; label: string; description?: string }[];
  fields:     FieldDef[];
  filterable_by_source: FilterableBySource;
};

//--- Template column + sort ---------------------------------------

export type ColumnKind   = 'identifier' | 'formula';
//--- 'number' renders decimals with locale thousand separators and NO
//--- currency sign, e.g. 1,234.56. Distinct from 'money' (prepends $) and
//--- 'int' (drops decimals). Use for lots, exposure as a raw figure,
//--- ratios, custom formulas where the $ symbol is unwanted.
export type ColumnFormat = 'money' | 'pct' | 'int' | 'text' | 'date' | 'number';

export type Column = {
  key:    string;
  label:  string;
  kind:   ColumnKind;
  format: ColumnFormat;
  source?: string;            // identifier-only
  expr?:   ExprNode;          // formula-only
  //--- When true on an identifier column, this column contributes to the row
  //--- bucket key. The engine groups by the tuple of all pivot_key columns in
  //--- column order. Backward compat: legacy templates lack this; on load the
  //--- first identifier is implicitly pivot_key=true.
  pivot_key?: boolean;
  //--- Pivot row filter — applied per pivot_key column. For user-source
  //--- identifiers (login, group, country, …) the predicate runs against
  //--- UserInfo; for symbol/ticket it runs against each DealRow before
  //--- bucketing.
  row_predicate?: Predicate | null;
};

export type SortSpec = {
  column_key: string;
  direction:  'asc' | 'desc';
  //--- Sort by |x| (absolute value) for numeric columns. Orthogonal to
  //--- direction: desc+abs = biggest magnitude on top, asc+abs = rows
  //--- nearest zero on top. Ignored for text cells.
  abs?:       boolean;
};

//--- Pre-aggregation per-login row filter. Each entry references a
//--- numeric column by its key; the engine evaluates that column's
//--- formula per login (BEFORE pivot bucket formation) and drops the
//--- login if any filter fails. AND semantics across multiple filters.
//--- Works correctly in both per_account and future pivot/aggregate
//--- modes — dropped logins never contribute to any sum/count.
export type RowFilterOp = 'gt' | 'gte' | 'eq' | 'neq' | 'lte' | 'lt';
export type RowFilter   = { column_key: string; op: RowFilterOp; value: number };

export type Template = {
  id: number;
  name: string;
  description: string;
  row_model: 'per_account';
  date_params: string[];
  columns: Column[];
  sort: SortSpec;
  row_filters?: RowFilter[];
  default_top_n: number;
  folder_id?: number | null;
  sort_order?: number;
  //--- v12 soft-delete. Templates hidden from the list when set; the row
  //--- still exists so referencing jobs / ready-mades can render the name.
  //--- Engine refuses to run a deleted template.
  deleted_at?: number | null;
  created_at: number;
  updated_at: number;
};

//--- Saved formula blueprint (reusable building block) -----------

export type FormulaBlueprint = {
  id: number;
  name: string;
  description: string;
  date_params: string[];
  expr: ExprNode;
  folder_id?: number | null;
  sort_order?: number;
  created_at: number;
  updated_at: number;
};

export type FormulaBlueprintInput = Omit<FormulaBlueprint, 'id' | 'created_at' | 'updated_at'>;

//--- Ready-made report (saved template+filter+date bundle) ----------

//--- Base presets stop at yesterday (sealed daily snapshot); the
//--- `_to_date` variants include today (partial snapshot). See
//--- backend Scheduler.cpp::ResolveRelative for the canonical formulas.
export type RelativePreset =
  | 'today' | 'yesterday'
  | 'last_n_days'  | 'last_n_days_to_date'
  | 'this_week'    | 'this_week_to_date'
  | 'last_week'
  | 'this_month'   | 'this_month_to_date'
  | 'last_month';

export type ReadyMadeReport = {
  id: number;
  name: string;
  description: string;
  template_id: number;
  account_filter_id: number | null;
  //--- Bound DepositFilter id (multi-bucket cash-flow preset). Used by
  //--- sum_deposit_amount / count_deposits / sum_deposit_abs aggregator
  //--- fields at run time. Null = unbound; those fields then return 0.
  deposit_filter_id: number | null;
  date_strategy: 'fixed' | 'relative';
  fixed_dates: Record<string, string>;     // e.g. { "date_from":"2026-04-01", "date_to":"2026-04-30" }
  relative_preset: RelativePreset;
  relative_n: number;
  top_n_override: number;
  folder_id?: number | null;
  sort_order?: number;
  created_at: number;
  updated_at: number;
};

export type ReadyMadeReportInput = Omit<ReadyMadeReport, 'id' | 'created_at' | 'updated_at'>;

export type ReadyMadeRunRequest = {
  dates?: Record<string, string>;
  account_filter_id?: number | null;
  deposit_filter_id?: number | null;
  top_n?: number;
  manager_id?: number;
};

//--- Scheduler -----------------------------------------------------

export type ScheduleFrequency = 'daily' | 'weekly' | 'monthly' | 'hourly';
//--- 'text'  - HTML-formatted summary message (bold title + KPI block)
//--- 'csv'   - csv file produced by the engine (server-persisted)
//--- 'xlsx'  - typed-cell spreadsheet generated on delivery from the CSV
//--- 'pdf'   - tabular PDF generated on delivery (Helvetica + Courier)
//--- 'image' - PNG screenshot of the result page (headless Chrome required)
export type ScheduleDeliveryFormat = 'csv' | 'text' | 'xlsx' | 'pdf' | 'image';

export type ScheduleEntry = {
  id: number;
  name: string;
  ready_made_id: number;
  frequency: ScheduleFrequency;
  time_hour: number;
  time_minute: number;
  day_of_week: number;
  day_of_month: number;
  every_n_hours: number;
  hours: number[];           // 0..23, empty = legacy every_n_hours (hourly only)
  days_of_week: number[];    // 0..6 (0=Sun), empty = every day
  telegram_chat_id: string;
  delivery_format: ScheduleDeliveryFormat;
  enabled: boolean;
  folder_id?: number | null;
  sort_order?: number;
  next_run_at: number;
  last_run_at: number;
  last_status: string;
  last_job_id: number | null;
  last_error: string;
  created_at: number;
  updated_at: number;
};

export type ScheduleEntryInput = Omit<ScheduleEntry,
  'id' | 'next_run_at' | 'last_run_at' | 'last_status' | 'last_job_id' | 'last_error' | 'created_at' | 'updated_at'>;

//--- App settings (Telegram) ---------------------------------------

export type TelegramSettings = {
  configured:        boolean;
  bot_token_masked:  string;
  default_chat_id:   string;
};

export type TelegramSettingsInput = {
  bot_token?:       string;          // empty/omitted → keep existing
  default_chat_id?: string;
};

export type TemplateInput = Omit<Template, 'id' | 'created_at' | 'updated_at'>;

//--- Template validation result ----------------------------------

export type ValidationError = {
  path:    string;       // e.g. "columns[2].expr.left"
  message: string;
};
export type ValidationResult = {
  ok:     boolean;
  errors: ValidationError[];
};

//--- Run / Job ----------------------------------------------------

export type JobStatus = 'queued' | 'running' | 'completed' | 'failed';

export type RunReportRequest = {
  template_id: number;
  manager_id:  number;
  account_filter_id?: number | null;
  //--- Active DepositFilter for sum_deposit_amount / count_deposits fields.
  //--- Null = unbound; those fields return 0.
  deposit_filter_id?: number | null;
  dates: Record<string, string>;     // YYYY-MM-DD per date_param name
  top_n?: number;
  account_filter_override?: {
    group_masks?: string[];
    group_regex?: string;
    login_min?: number | null;
    login_max?: number | null;
  };
};

export type ResultCell  = number | string | null;
export type ResultRow   = ResultCell[];
export type ResultPreview = {
  template_id:   number;
  template_name: string;
  columns:       Pick<Column, 'key' | 'label' | 'kind' | 'format'>[];
  rows:          ResultRow[];
  total_logins:  number;
  date_from?:    number;
  date_to?:      number;
};

export type ReportJob = {
  id: number;
  manager_id:  number;
  template_id: number;
  account_filter_id?: number | null;
  template_name?: string;
  params_json: string;
  status:      JobStatus;
  progress:    number;
  error_message?: string;
  created_at:  number;
  started_at?: number;
  completed_at?: number;
  csv_url?:    string;
  xlsx_url?:   string;
  preview?:    ResultPreview;
};
