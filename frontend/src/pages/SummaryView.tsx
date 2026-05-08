import { ReportsAPI } from '../api/reports';
import { DataTable, Column } from '../components/DataTable';
import { DownloadButtons } from '../components/DownloadButtons';
import { fmtMoney, fmtPct, fmtInt, fmtDate } from '../utils/format';
import type { ReportJob, SummaryPreview, SummaryDailyRow } from '../types';

export function SummaryView({ job }: { job: ReportJob }) {
  const preview = job.preview as SummaryPreview;
  const m = preview.metrics;

  const kpis: Array<[string, string, string?]> = [
    ['Monthly Deposit',           fmtMoney(m.monthly_deposit)],
    ['Monthly Withdrawal',        fmtMoney(m.monthly_withdrawal)],
    ['Monthly Net Deposit',       fmtMoney(m.monthly_net_deposit)],
    ["Today's Total Equity",      fmtMoney(m.todays_total_equity)],
    ["Yesterday's Total Equity",  fmtMoney(m.yesterdays_total_equity)],
    ['Equity Change %',           fmtPct(m.equity_change_pct), m.equity_change_pct >= 0 ? 'text-emerald-700' : 'text-red-600'],
    ['Daily New Account Openings',   fmtInt(m.daily_new_accounts)],
    ['Monthly New Account Openings', fmtInt(m.monthly_new_accounts)],
    ['Monthly Company PnL',       fmtMoney(m.monthly_company_pnl)],
  ];

  const cols: Column<SummaryDailyRow>[] = [
    { key: 'date',   header: 'Date',                  render: r => <span className="font-mono">{fmtDate(r.date)}</span> },
    { key: 'brand',  header: 'Brand',                 render: r => r.brand },
    { key: 'dep',    header: 'Deposit',                align: 'right', render: r => fmtMoney(r.deposit) },
    { key: 'wd',     header: 'Withdrawal',             align: 'right', render: r => fmtMoney(r.withdrawal) },
    { key: 'nd',     header: 'Net Deposit',            align: 'right', render: r => fmtMoney(r.net_deposit) },
    { key: 'cpl',    header: 'Closed PnL',             align: 'right', render: r => fmtMoney(r.closed_pnl) },
    { key: 'fpc',    header: 'Floating PnL Change',    align: 'right', render: r => fmtMoney(r.floating_pnl_change) },
    { key: 'neg',    header: 'Negative Equity Change', align: 'right', render: r => fmtMoney(r.negative_equity_change) },
    { key: 'eq',     header: "Today's Total Equity",   align: 'right', render: r => fmtMoney(r.todays_total_equity) },
    { key: 'na',     header: 'New Accounts',           align: 'right', render: r => fmtInt(r.new_accounts) },
    { key: 'cmp',    header: 'Company PnL',            align: 'right', render: r => <span className={r.company_pnl < 0 ? 'text-red-600' : 'text-emerald-700'}>{fmtMoney(r.company_pnl)}</span> },
  ];

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-ink-900">{preview.header}</h2>
        <DownloadButtons csvUrl={ReportsAPI.csvUrl(job.id)} xlsxUrl={job.xlsx_url ? ReportsAPI.xlsxUrl(job.id) : undefined} />
      </div>

      <div className="grid grid-cols-2 md:grid-cols-3 gap-3">
        {kpis.map(([label, value, color]) => (
          <div key={label} className="card p-4">
            <div className="text-xs text-ink-500 uppercase tracking-wide">{label}</div>
            <div className={`text-xl font-semibold mt-1 ${color ?? 'text-ink-900'}`}>{value}</div>
          </div>
        ))}
      </div>

      <DataTable columns={cols} rows={preview.daily} emptyText="No daily rows" />
    </div>
  );
}
