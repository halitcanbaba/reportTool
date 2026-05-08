import { ReportsAPI } from '../api/reports';
import { DataTable, Column } from '../components/DataTable';
import { DownloadButtons } from '../components/DownloadButtons';
import { fmtMoney } from '../utils/format';
import type { ReportJob, TopWinnerPreview, TopWinnerRow } from '../types';

export function TopWinnerView({ job }: { job: ReportJob }) {
  const preview = job.preview as TopWinnerPreview;

  const cols: Column<TopWinnerRow>[] = [
    { key: 'login',  header: 'Login', render: r => <span className="font-mono">{r.login}</span> },
    { key: 'dep',    header: 'Deposit',            align: 'right', render: r => fmtMoney(r.deposit) },
    { key: 'wd',     header: 'Withdrawal',         align: 'right', render: r => fmtMoney(r.withdrawal) },
    { key: 'nd',     header: 'Net Deposit',        align: 'right', render: r => fmtMoney(r.net_deposit) },
    { key: 'cpl',    header: 'Closed PL',          align: 'right', render: r => fmtMoney(r.closed_pl) },
    { key: 'fpc',    header: 'Floating PL Change', align: 'right', render: r => fmtMoney(r.floating_pl_change) },
    { key: 'wo',     header: 'Balance Writeoff',   align: 'right', render: r => fmtMoney(r.balance_writeoff) },
    { key: 'adj',    header: 'Trade Adjustments',  align: 'right', render: r => fmtMoney(r.trade_adjustments) },
    { key: 'eq',     header: 'Net Equity',         align: 'right', render: r => fmtMoney(r.net_equity) },
    { key: 'cmp',    header: 'Company PL',         align: 'right', render: r => <span className={r.company_pl < 0 ? 'text-red-600' : 'text-emerald-700'}>{fmtMoney(r.company_pl)}</span> },
  ];

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <div>
          <h2 className="text-xl font-semibold text-ink-900">{preview.header}</h2>
          <div className="text-xs text-ink-500 mt-1">{preview.total_logins.toLocaleString()} logins searched</div>
        </div>
        <DownloadButtons csvUrl={ReportsAPI.csvUrl(job.id)} xlsxUrl={job.xlsx_url ? ReportsAPI.xlsxUrl(job.id) : undefined} />
      </div>
      <DataTable columns={cols} rows={preview.rows} emptyText="No results" />
    </div>
  );
}
