import { useState } from 'react';
import { useManagers } from '../hooks/useManagers';
import { ReportsAPI } from '../api/reports';
import { useReportJob } from '../hooks/useReportJob';
import { TopWinnerView } from './TopWinnerView';
import { SummaryView } from './SummaryView';

type Tab = 'top_winner' | 'summary';

const today = () => new Date().toISOString().slice(0, 10);
const firstOfMonth = () => today().slice(0, 7) + '-01';

export function ReportRunnerPage() {
  const { items: managers, loading } = useManagers();
  const [tab, setTab] = useState<Tab>('top_winner');
  const [managerId, setManagerId] = useState<number | null>(null);
  const [from, setFrom] = useState(firstOfMonth());
  const [to, setTo] = useState(today());
  const [topN, setTopN] = useState(20);
  const [month, setMonth] = useState(today().slice(0, 7));
  const [jobId, setJobId] = useState<number | null>(null);
  const [running, setRunning] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  const { job } = useReportJob(jobId);

  const run = async () => {
    if (!managerId) { setErr('Pick a manager first.'); return; }
    if (tab === 'top_winner') {
      if (!from) { setErr('Date from is empty or invalid (e.g. 04/31 doesn’t exist — April has 30 days).'); return; }
      if (!to)   { setErr('Date to is empty or invalid.'); return; }
      if (from > to) { setErr('Date from must be ≤ Date to.'); return; }
      if (!topN || topN < 1) { setErr('Top N must be ≥ 1.'); return; }
    } else {
      if (!month) { setErr('Pick a month (YYYY-MM).'); return; }
    }
    setRunning(true); setErr(null);
    try {
      if (tab === 'top_winner') {
        const r = await ReportsAPI.runTopWinner({ manager_id: managerId, date_from: from, date_to: to, top_n: topN });
        setJobId(r.job_id);
      } else {
        const r = await ReportsAPI.runSummary({ manager_id: managerId, month });
        setJobId(r.job_id);
      }
    } catch (e: any) { setErr(e.message ?? 'run failed'); }
    finally { setRunning(false); }
  };

  return (
    <div>
      <div className="mb-6">
        <h1 className="text-2xl font-semibold text-ink-900">Reports</h1>
        <p className="text-sm text-ink-500 mt-1">Generate Top Winner and Summary reports for a manager.</p>
      </div>

      <div className="grid grid-cols-1 lg:grid-cols-[320px,1fr] gap-6">
        <div className="space-y-4">
          <div className="card p-5">
            <div className="flex border-b border-ink-100 -mx-5 -mt-5 px-5 mb-4">
              {(['top_winner', 'summary'] as Tab[]).map(t => (
                <button key={t} onClick={() => setTab(t)}
                        className={`px-3 py-3 text-sm font-medium border-b-2 ${tab === t ? 'border-ink-900 text-ink-900' : 'border-transparent text-ink-500 hover:text-ink-700'}`}>
                  {t === 'top_winner' ? 'Top Winner' : 'Summary'}
                </button>
              ))}
            </div>

            <label className="label">Manager</label>
            <select className="input mb-3" value={managerId ?? ''} onChange={e => setManagerId(e.target.value ? Number(e.target.value) : null)}>
              <option value="">{loading ? 'Loading…' : '-- choose --'}</option>
              {managers.map(m => <option key={m.id} value={m.id}>{m.name} ({m.brand} / {m.region})</option>)}
            </select>

            {tab === 'top_winner' ? (
              <>
                <label className="label">Date from</label>
                <input type="date" className="input mb-3" value={from} onChange={e => setFrom(e.target.value)} />
                <label className="label">Date to</label>
                <input type="date" className="input mb-3" value={to} onChange={e => setTo(e.target.value)} />
                <label className="label">Top N</label>
                <input type="number" min={1} max={500} className="input mb-3" value={topN} onChange={e => setTopN(Number(e.target.value))} />
              </>
            ) : (
              <>
                <label className="label">Month</label>
                <input type="month" className="input mb-3" value={month} onChange={e => setMonth(e.target.value)} />
              </>
            )}

            <button onClick={run} disabled={running || !managerId} className="btn-primary w-full">
              {running ? 'Submitting…' : 'Run report'}
            </button>
            {err && <div className="text-xs text-red-600 mt-2">{err}</div>}
          </div>
        </div>

        <div>
          {!jobId && (
            <div className="card p-12 text-center text-ink-400">Pick a manager + parameters and click Run.</div>
          )}

          {jobId && job && job.status !== 'completed' && job.status !== 'failed' && (
            <div className="card p-5">
              <div className="flex items-center justify-between mb-2">
                <div className="font-medium">Job #{job.id} — {job.status}</div>
                <div className="text-xs text-ink-500">{Math.round(job.progress * 100)}%</div>
              </div>
              <div className="h-2 bg-ink-100 rounded-full overflow-hidden">
                <div className="h-full bg-ink-900 transition-all" style={{ width: `${job.progress * 100}%` }} />
              </div>
            </div>
          )}

          {jobId && job && job.status === 'failed' && (
            <div className="card p-5 border-red-200 bg-red-50 text-red-800">
              <div className="font-medium mb-1">Job failed</div>
              <div className="text-xs font-mono">{job.error_message}</div>
            </div>
          )}

          {jobId && job && job.status === 'completed' && job.preview && (
            <>
              {job.kind === 'top_winner'
                ? <TopWinnerView job={job} />
                : <SummaryView job={job} />}
            </>
          )}
        </div>
      </div>
    </div>
  );
}
