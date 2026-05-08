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

export type TopWinnerRow = {
  login: number;
  deposit: number;
  withdrawal: number;
  net_deposit: number;
  closed_pl: number;
  floating_pl_change: number;
  balance_writeoff: number;
  trade_adjustments: number;
  net_equity: number;
  company_pl: number;
};

export type SummaryDailyRow = {
  date: number;
  brand: string;
  deposit: number;
  withdrawal: number;
  net_deposit: number;
  closed_pnl: number;
  floating_pnl_change: number;
  negative_equity_change: number;
  todays_total_equity: number;
  new_accounts: number;
  company_pnl: number;
};

export type SummaryMetrics = {
  brand: string;
  monthly_deposit: number;
  monthly_withdrawal: number;
  monthly_net_deposit: number;
  todays_total_equity: number;
  yesterdays_total_equity: number;
  equity_change_pct: number;
  daily_new_accounts: number;
  monthly_new_accounts: number;
  monthly_company_pnl: number;
};

export type JobStatus = 'queued' | 'running' | 'completed' | 'failed';
export type ReportKind = 'top_winner' | 'summary';

export type ReportJob = {
  id: number;
  manager_id: number;
  kind: ReportKind;
  params_json: string;
  status: JobStatus;
  progress: number;
  error_message?: string;
  created_at: number;
  started_at?: number;
  completed_at?: number;
  csv_url?: string;
  xlsx_url?: string;
  preview?: TopWinnerPreview | SummaryPreview;
};

export type TopWinnerPreview = {
  header: string;
  date_from: number;
  date_to: number;
  total_logins: number;
  rows: TopWinnerRow[];
};

export type SummaryPreview = {
  header: string;
  metrics: SummaryMetrics;
  daily: SummaryDailyRow[];
};
