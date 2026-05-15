import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { SchedulesAPI } from '../api/schedules';
import { ReadyMadeAPI } from '../api/readyMade';
import { TelegramSettingsCard } from '../components/TelegramSettingsCard';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import type { ScheduleEntry, ReadyMadeReport } from '../types';

function describeFreq(s: ScheduleEntry): string {
  const t = `${String(s.time_hour).padStart(2, '0')}:${String(s.time_minute).padStart(2, '0')} UTC`;
  switch (s.frequency) {
    case 'daily':   return `Daily at ${t}`;
    case 'weekly':  return `Weekly on ${['Sun','Mon','Tue','Wed','Thu','Fri','Sat'][s.day_of_week] ?? '?'} at ${t}`;
    case 'monthly': return `Monthly on day ${s.day_of_month} at ${t}`;
    case 'hourly':  return `Every ${s.every_n_hours} hour(s)`;
    default:        return s.frequency;
  }
}

function statusBadge(s: string) {
  const map: Record<string, string> = {
    completed:  'bg-emerald-50 text-emerald-700 border-emerald-200',
    dispatched: 'bg-blue-50 text-blue-700 border-blue-200',
    failed:     'bg-red-50 text-red-700 border-red-200',
    pending:    'bg-amber-50 text-amber-700 border-amber-200',
    '':         'bg-ink-50 text-ink-500 border-ink-200',
  };
  const cls = map[s] ?? map[''];
  return <span className={`text-xs px-2 py-0.5 rounded border ${cls}`}>{s || '—'}</span>;
}

export function ScheduleListPage() {
  const [items, setItems] = useState<ScheduleEntry[]>([]);
  const [readyMades, setReadyMades] = useState<Map<number, ReadyMadeReport>>(new Map());
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try {
      const [scs, rms] = await Promise.all([SchedulesAPI.list(), ReadyMadeAPI.list()]);
      setItems(scs);
      setReadyMades(new Map(rms.map(r => [r.id, r])));
      setError(null);
    } catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete schedule "${name}"?`)) return;
    try { await SchedulesAPI.remove(id); reload(); }
    catch (e: any) { alert(e.message ?? 'delete failed'); }
  };

  const onToggle = async (s: ScheduleEntry) => {
    try {
      await SchedulesAPI.update(s.id, { enabled: !s.enabled });
      reload();
    } catch (e: any) { alert(e.message ?? 'update failed'); }
  };

  const onRunNow = async (s: ScheduleEntry) => {
    if (!confirm(`Run "${s.name}" now? A job will be queued and the next firing will be skipped.`)) return;
    try { await SchedulesAPI.runNow(s.id); reload(); }
    catch (e: any) { alert(e.message ?? 'run-now failed'); }
  };

  const onDuplicate = async (s: ScheduleEntry) => {
    try {
      const full = await SchedulesAPI.get(s.id);
      const { id, next_run_at, last_run_at, last_status, last_job_id, last_error,
              created_at, updated_at, ...rest } = full;
      //--- Duplicate starts disabled so the operator can review timing before
      //--- it begins firing alongside the original.
      await SchedulesAPI.create({
        ...rest,
        name:    copyName(s.name, items.map(i => i.name)),
        enabled: false,
      });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  return (
    <div className="grid grid-cols-3 gap-6">
      <div className="col-span-2">
        <div className="flex items-center justify-between mb-6">
          <div>
            <h1 className="text-2xl font-semibold text-ink-900">Scheduler</h1>
            <p className="text-sm text-ink-500 mt-1">Recurring ready-made reports delivered via Telegram. All times in UTC (MT5 broker trading day).</p>
          </div>
          <Link to="/schedules/new" className="btn-primary">+ New schedule</Link>
        </div>

        {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
        {loading && <div className="text-ink-400 text-sm">Loading…</div>}

        {!loading && items.length === 0 && (
          <div className="card p-12 text-center">
            <div className="text-ink-400 mb-4">No schedules yet.</div>
            <Link to="/schedules/new" className="btn-primary">Create the first schedule</Link>
          </div>
        )}

        {!loading && items.length > 0 && (
          <div className="card overflow-hidden">
            <table className="min-w-full text-sm">
              <thead className="bg-ink-50 border-b border-ink-100">
                <tr>
                  <th className="px-3 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Name</th>
                  <th className="px-3 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Ready-made</th>
                  <th className="px-3 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Frequency</th>
                  <th className="px-3 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Next run</th>
                  <th className="px-3 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Last status</th>
                  <th className="px-3 py-3"></th>
                </tr>
              </thead>
              <tbody>
                {items.map(s => {
                  const rm = readyMades.get(s.ready_made_id);
                  return (
                    <tr key={s.id} className={`border-b border-ink-50 last:border-0 ${s.enabled ? '' : 'opacity-60'}`}>
                      <td className="px-3 py-3">
                        <div className="font-medium flex items-center gap-2">
                          <input type="checkbox" checked={s.enabled} onChange={() => onToggle(s)} title="enabled" />
                          {s.name}
                        </div>
                        {s.telegram_chat_id && <div className="text-[11px] text-ink-500 font-mono mt-0.5">→ {s.telegram_chat_id}</div>}
                      </td>
                      <td className="px-3 py-3 text-xs">{rm?.name ?? <span className="text-red-600">missing #{s.ready_made_id}</span>}</td>
                      <td className="px-3 py-3 text-xs">{describeFreq(s)}</td>
                      <td className="px-3 py-3 text-xs text-ink-600">{s.next_run_at ? fmtDateTime(s.next_run_at) : '—'}</td>
                      <td className="px-3 py-3">
                        <div className="flex items-center gap-2">
                          {statusBadge(s.last_status)}
                          {s.last_run_at > 0 && <span className="text-[11px] text-ink-500">{fmtDateTime(s.last_run_at)}</span>}
                        </div>
                        {s.last_error && <div className="text-[11px] text-red-700 font-mono mt-1 line-clamp-2" title={s.last_error}>{s.last_error}</div>}
                      </td>
                      <td className="px-3 py-3 text-right whitespace-nowrap">
                        <button onClick={() => onRunNow(s)} className="btn-secondary text-xs px-2 py-1 mr-1">Run now</button>
                        <Link to={`/schedules/${s.id}/edit`} className="btn-secondary text-xs px-2 py-1 mr-1">Edit</Link>
                        <button onClick={() => onDuplicate(s)} className="btn-secondary text-xs px-2 py-1 mr-1">Duplicate</button>
                        <button onClick={() => onDelete(s.id, s.name)} className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50">Delete</button>
                      </td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <div className="col-span-1 space-y-4">
        <TelegramSettingsCard />
        <div className="card p-4 space-y-2 text-xs text-ink-600">
          <div className="font-semibold text-ink-700 uppercase tracking-wide">How delivery works</div>
          <ul className="list-disc list-inside space-y-1">
            <li>Scheduler ticks every 60s.</li>
            <li>When a schedule is due, a job is queued.</li>
            <li>When the job completes, its CSV is sent via the bot.</li>
            <li>If the file is &gt;49MB, a summary message is sent instead.</li>
            <li>Per-schedule <code>chat_id</code> overrides the default.</li>
          </ul>
        </div>
      </div>
    </div>
  );
}
