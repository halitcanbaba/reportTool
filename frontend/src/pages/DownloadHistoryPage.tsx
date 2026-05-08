import { useEffect, useState } from 'react';
import { ReportsAPI } from '../api/reports';
import { StatusPill } from '../components/StatusPill';
import { fmtDateTime } from '../utils/format';
import type { ReportJob } from '../types';

export function DownloadHistoryPage() {
  const [jobs, setJobs] = useState<ReportJob[]>([]);
  const [loading, setLoading] = useState(true);

  const reload = async () => {
    setLoading(true);
    try { setJobs(await ReportsAPI.listJobs(100)); }
    finally { setLoading(false); }
  };

  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number) => {
    if (!confirm(`Delete job #${id}? Output files will also be removed.`)) return;
    await ReportsAPI.removeJob(id); reload();
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">History</h1>
          <p className="text-sm text-ink-500 mt-1">Past report runs and downloadable artefacts.</p>
        </div>
        <button className="btn-secondary" onClick={reload}>Refresh</button>
      </div>

      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      <div className="card overflow-hidden">
        <table className="min-w-full text-sm">
          <thead className="bg-ink-50 border-b border-ink-100">
            <tr>
              <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">#</th>
              <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Kind</th>
              <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Status</th>
              <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Created</th>
              <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Completed</th>
              <th className="px-4 py-3"></th>
            </tr>
          </thead>
          <tbody>
            {jobs.length === 0 && (
              <tr><td colSpan={6} className="px-4 py-12 text-center text-ink-400">No jobs yet.</td></tr>
            )}
            {jobs.map(j => (
              <tr key={j.id} className="border-b border-ink-50 last:border-0">
                <td className="px-4 py-3 font-mono">#{j.id}</td>
                <td className="px-4 py-3">{j.kind}</td>
                <td className="px-4 py-3"><StatusPill status={j.status} /></td>
                <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(j.created_at)}</td>
                <td className="px-4 py-3 text-xs text-ink-500">{j.completed_at ? fmtDateTime(j.completed_at) : '—'}</td>
                <td className="px-4 py-3">
                  <div className="flex items-center gap-2 justify-end">
                    {j.csv_url  && <a className="btn-secondary text-xs px-2 py-1" href={ReportsAPI.csvUrl(j.id)}  download>CSV</a>}
                    {j.xlsx_url && <a className="btn-secondary text-xs px-2 py-1" href={ReportsAPI.xlsxUrl(j.id)} download>XLSX</a>}
                    <button className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50" onClick={() => onDelete(j.id)}>Delete</button>
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
