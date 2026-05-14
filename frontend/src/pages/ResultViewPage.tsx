import { useParams } from 'react-router-dom';
import { useReportJob } from '../hooks/useReportJob';
import { ReportsAPI } from '../api/reports';
import { StatusPill } from '../components/StatusPill';
import { Breadcrumbs } from '../components/Breadcrumbs';
import { fmtDateTime, fmtMoney, fmtPct, fmtInt, fmtDate } from '../utils/format';

const fmtCell = (v: number | string | null, fmt: string): string => {
  if (v == null) return '';
  if (typeof v === 'string') return v;
  switch (fmt) {
    case 'money': return fmtMoney(v);
    case 'pct':   return fmtPct(v);
    case 'int':   return fmtInt(v);
    case 'date':  return fmtDate(v);
    default:      return String(v);
  }
};

export function ResultViewPage() {
  const { id } = useParams();
  const jobId = id != null ? Number(id) : null;
  const { job, error } = useReportJob(jobId);

  if (error) return <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>;
  if (!job)  return <div className="text-sm text-ink-400">Loading…</div>;

  const preview = job.preview;

  return (
    <div className="space-y-4">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'History', to: '/history' },
            { label: `Job #${job.id}${preview ? ` — ${preview.template_name}` : ''}` },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">
            Job #{job.id}{preview ? `: ${preview.template_name}` : ''}
          </h1>
          <div className="mt-1 flex items-center gap-3 text-sm text-ink-500">
            <StatusPill status={job.status} />
            <span>created {fmtDateTime(job.created_at)}</span>
            {job.completed_at ? <span>· finished {fmtDateTime(job.completed_at)}</span> : null}
          </div>
        </div>
        <div className="flex gap-2">
          {job.csv_url && <a className="btn-primary text-sm" href={ReportsAPI.csvUrl(job.id)} download>Download CSV</a>}
        </div>
      </div>

      {job.status === 'queued' || job.status === 'running' ? (
        <div className="card p-6">
          <div className="text-sm text-ink-600">In progress…</div>
          <div className="mt-2 w-full bg-ink-100 rounded h-2 overflow-hidden">
            <div className="bg-blue-500 h-full transition-all" style={{ width: `${Math.round((job.progress || 0) * 100)}%` }} />
          </div>
          <div className="text-xs text-ink-500 mt-1 text-right">{Math.round((job.progress || 0) * 100)}%</div>
        </div>
      ) : null}

      {job.status === 'failed' && (
        <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">
          {job.error_message || 'job failed'}
        </div>
      )}

      {preview && preview.columns && (
        <div className="card overflow-auto">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                {preview.columns.map(c => (
                  <th key={c.key} className={`px-3 py-2 text-${c.format === 'text' ? 'left' : 'right'} font-medium text-ink-600 uppercase text-xs tracking-wide`}>
                    {c.label}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {preview.rows.map((row, i) => (
                <tr key={i} className="border-b border-ink-50 last:border-0">
                  {preview.columns.map((c, k) => {
                    const cell = row[k];
                    const align = c.format === 'text' ? 'text-left' : 'text-right tabular-nums';
                    return (
                      <td key={c.key} className={`px-3 py-2 ${align} ${c.format === 'text' ? '' : 'font-mono'}`}>
                        {fmtCell(cell ?? null, c.format)}
                      </td>
                    );
                  })}
                </tr>
              ))}
              {preview.rows.length === 0 && (
                <tr><td className="px-3 py-12 text-center text-ink-400" colSpan={preview.columns.length}>No rows.</td></tr>
              )}
            </tbody>
          </table>
        </div>
      )}

      {preview && (
        <div className="text-xs text-ink-500">
          Total logins evaluated: {preview.total_logins}
          {preview.rows.length === 200 && ' · preview limited to first 200 rows; download CSV for full set'}
        </div>
      )}
    </div>
  );
}
