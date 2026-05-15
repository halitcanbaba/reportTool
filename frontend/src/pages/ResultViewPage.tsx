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

//--- Pull dates / top_n out of the job's params_json for the summary header.
//--- Returns an entries list so the original date-param order survives.
function parseRunSummary(params_json: string): { dates: [string, string][]; topN?: number } {
  try {
    const j = JSON.parse(params_json);
    const d = j?.dates && typeof j.dates === 'object' ? j.dates : {};
    const dates = Object.entries(d).filter(([, v]) => typeof v === 'string') as [string, string][];
    const topN = typeof j?.top_n === 'number' ? j.top_n : undefined;
    return { dates, topN };
  } catch { return { dates: [] }; }
}

//--- Try to pick (from, to) out of an arbitrary date-param map. Looks for
//--- the canonical `date_from`/`date_to` pair first, then falls back to the
//--- first two entries in their declared order.
function pickFromTo(dates: [string, string][]): { from?: string; to?: string } | null {
  if (dates.length === 0) return null;
  const map = new Map(dates);
  const from = map.get('date_from') ?? (dates[0]?.[1]);
  const to   = map.get('date_to')   ?? (dates.length >= 2 ? dates[1][1] : undefined);
  if (!from) return null;
  return { from, to };
}

export function ResultViewPage() {
  const { id } = useParams();
  const jobId = id != null ? Number(id) : null;
  const { job, error } = useReportJob(jobId);

  if (error) return <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>;
  if (!job)  return <div className="text-sm text-ink-400">Loading…</div>;

  const preview = job.preview;
  const { dates, topN } = parseRunSummary(job.params_json);
  const range = pickFromTo(dates);
  const templateName = preview?.template_name ?? job.template_name ?? 'Template';

  return (
    <div className="space-y-4">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'History', to: '/history' },
            { label: `Job #${job.id} — ${templateName}` },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">
            Job #{job.id}: {templateName}
          </h1>
          {range && (
            <div className="mt-1 text-sm text-ink-700">
              Ran <span className="font-semibold">{templateName}</span> for{' '}
              <span className="font-mono">{range.from}</span>
              {range.to && range.to !== range.from && (
                <> → <span className="font-mono">{range.to}</span></>
              )}
            </div>
          )}
          <div className="mt-1 flex flex-wrap items-center gap-x-3 gap-y-1 text-sm text-ink-600">
            <StatusPill status={job.status} />
            {dates.map(([name, val]) => (
              <span key={name} className="inline-flex items-center gap-1">
                <span className="text-ink-500 font-mono text-xs">{name}</span>
                <span className="font-mono">{val}</span>
              </span>
            ))}
            {topN !== undefined && topN > 0 && (
              <span className="inline-flex items-center gap-1">
                <span className="text-ink-500 font-mono text-xs">top</span>
                <span className="font-mono">{topN}</span>
              </span>
            )}
          </div>
          <div className="mt-0.5 text-[11px] text-ink-400">
            created {fmtDateTime(job.created_at)}
            {job.completed_at ? <> · finished {fmtDateTime(job.completed_at)}</> : null}
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
