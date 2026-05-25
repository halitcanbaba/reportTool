import { useEffect, useRef, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { SchedulesAPI } from '../api/schedules';
import { ReadyMadeAPI } from '../api/readyMade';
import { TelegramSettingsCard } from '../components/TelegramSettingsCard';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import type { ScheduleEntry, ReadyMadeReport, ScheduleDeliveryFormat } from '../types';
import { FolderedCard, type FolderedCol } from '../components/FolderedCard';
import { IconButton, IconSchedule, IconPlay, IconEdit, IconDuplicate, IconDelete } from '../components/icons';
import { FoldersAPI } from '../api/folders';
import type { Folder } from '../types';

const DOW_ABBR = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];

//--- Compress a sorted day-of-week list into a friendly label.
//--- 0=Sun..6=Sat. [1,2,3,4,5] → "weekdays"; [0,6] → "weekends".
function describeDows(dows: number[]): string {
  if (!dows.length) return '';
  const set = new Set(dows);
  const weekdays = [1,2,3,4,5].every(d => set.has(d)) && !set.has(0) && !set.has(6);
  const weekends = set.has(0) && set.has(6) && set.size === 2;
  if (set.size === 7) return 'every day';
  if (weekdays) return 'weekdays';
  if (weekends) return 'weekends';
  return dows.map(d => DOW_ABBR[d]).join(', ');
}

function describeFreq(s: ScheduleEntry): string {
  const mm = String(s.time_minute).padStart(2, '0');
  const t = `${String(s.time_hour).padStart(2, '0')}:${mm} UTC`;
  const dowSuffix = (s.days_of_week && s.days_of_week.length)
    ? ` · ${describeDows(s.days_of_week)}`
    : '';
  switch (s.frequency) {
    case 'daily':   return `Daily at ${t}${dowSuffix}`;
    case 'weekly':  return `Weekly on ${DOW_ABBR[s.day_of_week] ?? '?'} at ${t}`;
    case 'monthly': return `Monthly on day ${s.day_of_month} at ${t}`;
    case 'hourly': {
      const hours = s.hours && s.hours.length
        ? s.hours.map(h => `${String(h).padStart(2,'0')}:${mm}`).join(', ')
        : null;
      const head = hours ? `Hourly at ${hours}` : `Every ${s.every_n_hours} hour(s)`;
      return `${head}${dowSuffix}`;
    }
    default:        return s.frequency;
  }
}

function statusBadge(s: string) {
  const map: Record<string, string> = {
    completed:  'bg-emerald-50 text-emerald-700 border-emerald-200',
    dispatched: 'bg-blue-50 text-blue-700 border-blue-200 animate-pulse',
    delivering: 'bg-blue-50 text-blue-700 border-blue-200 animate-pulse',
    queued:     'bg-amber-50 text-amber-700 border-amber-200 animate-pulse',
    failed:     'bg-red-50 text-red-700 border-red-200',
    pending:    'bg-amber-50 text-amber-700 border-amber-200',
    '':         'bg-ink-50 text-ink-500 border-ink-200',
  };
  const cls = map[s] ?? map[''];
  return <span className={`text-xs px-2 py-0.5 rounded border ${cls}`}>{s || '—'}</span>;
}

export function ScheduleListPage() {
  const nav = useNavigate();
  const [items, setItems] = useState<ScheduleEntry[]>([]);
  const [readyMades, setReadyMades] = useState<Map<number, ReadyMadeReport>>(new Map());
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  //--- Schedules the user just triggered, with their click timestamps.
  //--- A ref (not React state) so reload() sees the latest value
  //--- synchronously — state setters are queued, the very next reload()
  //--- after onRunNow would otherwise read a stale closure that doesn't
  //--- include the just-clicked id and instantly overwrite the optimistic
  //--- 'queued' chip with whatever the backend returned.
  const pendingRunRef = useRef<Map<number, number>>(new Map());

  //--- Minimum visible time for the 'queued' chip after a manual run.
  //--- Without this, a fast pipeline (csv format, scheduler tick fires
  //--- right after the click) flips the row from queued straight to
  //--- completed in ~3 seconds and the user perceives no feedback.
  const QUEUED_DWELL_MS = 3000;

  //--- `silent=true` skips the loading spinner so background polls (after
  //--- Run-Now) update items in place without hiding the table — otherwise
  //--- every 5s tick blanks the rows to "Loading…" and the status-badge
  //--- transitions queued -> dispatched -> completed flash by invisibly.
  const reload = async (silent = false) => {
    if (!silent) setLoading(true);
    try {
      const [scs, rms] = await Promise.all([SchedulesAPI.list(), ReadyMadeAPI.list()]);
      setItems(prevItems => {
        const prevById = new Map(prevItems.map(it => [it.id, it]));
        return scs.map(s => {
          const startedAt = pendingRunRef.current.get(s.id);
          if (startedAt == null) return s;

          const elapsed = Date.now() - startedAt;
          //--- Phase 1 — dwell window: keep the chip visibly 'queued' no
          //--- matter what the backend says, so a sub-3-second pipeline
          //--- still surfaces the "click registered" feedback.
          if (elapsed < QUEUED_DWELL_MS) {
            return { ...s, last_status: 'queued' };
          }

          //--- Phase 2 — past dwell. If the backend is still flat (''),
          //--- or still showing the prior 'completed' for our schedule
          //--- (its last_run_at is older than our click), hold queued.
          const prev = prevById.get(s.id);
          const prevWasQueued = prev?.last_status === 'queued';
          const serverHasFreshRun = s.last_run_at * 1000 >= startedAt - 2000;
          const serverBehind = s.last_status === ''
                            || (s.last_status === 'completed' && prevWasQueued && !serverHasFreshRun);
          if (serverBehind) return { ...s, last_status: 'queued' };

          //--- Phase 3 — backend actually advanced for THIS run. Release
          //--- the ref so subsequent polls let backend authority through.
          if (serverHasFreshRun) pendingRunRef.current.delete(s.id);
          return s;
        });
      });
      setReadyMades(new Map(rms.map(r => [r.id, r])));
      setError(null);
    } catch (e: any) { setError(e.message ?? 'failed'); }
    finally { if (!silent) setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete schedule "${name}"?`)) return;
    try { await SchedulesAPI.remove(id); reload(); }
    catch (e: any) { alert(e.message ?? 'delete failed'); }
  };

  const onToggle = async (s: ScheduleEntry) => {
    try { await SchedulesAPI.update(s.id, { enabled: !s.enabled }); reload(); }
    catch (e: any) { alert(e.message ?? 'update failed'); }
  };

  const onRunNow = async (s: ScheduleEntry) => {
    if (!confirm(`Run "${s.name}" now? A job will be queued and the next firing will be skipped.`)) return;

    //--- Synchronous ref update so the very next reload() (queued behind
    //--- the API call) reads the latest pending set. State-only tracking
    //--- would lose this race because setState is queued and the closure
    //--- captured by reload reflects the PREVIOUS render's snapshot.
    const startedAt = Date.now();
    pendingRunRef.current.set(s.id, startedAt);

    //--- Optimistic chip on the row itself for instant feedback. The
    //--- dwell logic in reload() then HOLDS this chip for at least
    //--- QUEUED_DWELL_MS so even an instant pipeline (csv format +
    //--- favourable tick timing) still flashes queued visibly.
    setItems(prev => prev.map(x => x.id === s.id
      ? { ...x, last_status: 'queued', last_error: '' }
      : x));

    try {
      await SchedulesAPI.runNow(s.id);
    } catch (e: any) {
      pendingRunRef.current.delete(s.id);
      setItems(prev => prev.map(x => x.id === s.id ? { ...x, last_status: s.last_status } : x));
      alert(e.message ?? 'run-now failed');
      return;
    }

    //--- Don't reload right away — wait for the dwell window so the
    //--- 'queued' chip stays visible. Polls then track dispatched →
    //--- delivering → completed transitions. The scheduler tick is ≤60s
    //--- so dispatch lands within 60s and delivery within ~60s more;
    //--- a 5min poll window covers the slowest realistic round-trip
    //--- (image format + slow Telegram upload). Stop early once the
    //--- schedule reaches a terminal state so we don't burn cycles.
    const POLL_WINDOW_MS = 5 * 60_000;
    const tick = async () => {
      const elapsed = Date.now() - startedAt;
      if (elapsed > POLL_WINDOW_MS) {
        pendingRunRef.current.delete(s.id);
        return;
      }
      await reload(true);
      //--- Peek at the latest items via the setter callback; if the row
      //--- has reached a fresh-run terminal state ('completed'/'failed'
      //--- with last_run_at past the click), we're done.
      let reachedTerminal = false;
      setItems(curr => {
        const row = curr.find(x => x.id === s.id);
        if (row) {
          const fresh = row.last_run_at * 1000 >= startedAt - 2000;
          const term  = row.last_status === 'completed' || row.last_status === 'failed';
          if (fresh && term) reachedTerminal = true;
        }
        return curr;
      });
      if (reachedTerminal) {
        pendingRunRef.current.delete(s.id);
        return;
      }
      //--- 2s polls in the first 30s catch the scheduler-tick handoff;
      //--- then back off to 5s for the rest of the window.
      setTimeout(tick, elapsed < 30_000 ? 2000 : 5000);
    };
    //--- First poll AFTER the dwell window so the queued chip survives.
    setTimeout(tick, QUEUED_DWELL_MS + 200);
  };

  const onDuplicate = async (s: ScheduleEntry) => {
    try {
      const full = await SchedulesAPI.get(s.id);
      const { id: _id, next_run_at: _n, last_run_at: _l, last_status: _ls, last_job_id: _lj, last_error: _le,
              created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _n; void _l; void _ls; void _lj; void _le; void _c; void _u;
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

  const duplicateFolder = async (folder: Folder, rows: ScheduleEntry[]) => {
    const folders = await FoldersAPI.list('schedule');
    const dup = await FoldersAPI.create({
      entity_type: 'schedule',
      name: copyName(folder.name, folders.map(f => f.name)),
    });
    const existing = items.map(i => i.name);
    for (const r of rows) {
      const full = await SchedulesAPI.get(r.id);
      const { id: _id, next_run_at: _n, last_run_at: _l, last_status: _ls, last_job_id: _lj, last_error: _le,
              created_at: _c, updated_at: _u, folder_id: _f, ...rest } = full as any;
      void _id; void _n; void _l; void _ls; void _lj; void _le; void _c; void _u; void _f;
      const created = await SchedulesAPI.create({
        ...rest,
        name:    copyName(r.name, existing),
        enabled: false,
      });
      await FoldersAPI.move('schedule', (created as any).id, dup.id);
      existing.push(((created as any).name ?? ''));
    }
    reload();
  };

  const columns: FolderedCol<ScheduleEntry>[] = [
    {
      key: 'name', header: 'Name', searchable: true,
      searchValue: s => `${s.name} ${s.telegram_chat_id ?? ''}`,
      render: s => (
        <div className="flex items-start gap-2">
          <IconSchedule className="text-ink-500 shrink-0 mt-0.5" />
          <div className="min-w-0">
            <div className="font-medium flex items-center gap-2">
              <input type="checkbox" checked={s.enabled} onChange={() => onToggle(s)} title="enabled"
                     onPointerDown={e => e.stopPropagation()}
                     onClick={e => e.stopPropagation()} />
              {s.name}
            </div>
            <InlineChatId value={s.telegram_chat_id} onSave={async (v) => {
              await SchedulesAPI.update(s.id, { telegram_chat_id: v });
              reload();
            }} />
          </div>
        </div>
      ),
    },
    {
      key: 'delivery', header: 'Send as',
      searchValue: s => s.delivery_format,
      sortValue: s => s.delivery_format,
      render: s => (
        <InlineSelect<ScheduleDeliveryFormat>
          value={s.delivery_format}
          options={[
            { value: 'csv',   label: 'CSV' },
            { value: 'xlsx',  label: 'XLSX' },
            { value: 'pdf',   label: 'PDF' },
            { value: 'image', label: 'Screenshot' },
            { value: 'text',  label: 'Text' },
          ]}
          onSave={async (v) => {
            await SchedulesAPI.update(s.id, { delivery_format: v });
            reload();
          }} />
      ),
    },
    {
      key: 'ready_made', header: 'Ready-made', searchable: true,
      searchValue: s => readyMades.get(s.ready_made_id)?.name ?? '',
      render: s => {
        const rm = readyMades.get(s.ready_made_id);
        return <span className="text-xs">{rm?.name ?? <span className="text-red-600">missing #{s.ready_made_id}</span>}</span>;
      },
    },
    {
      key: 'freq', header: 'Frequency',
      searchValue: s => describeFreq(s),
      render: s => <span className="text-xs">{describeFreq(s)}</span>,
    },
    {
      key: 'next', header: 'Next run',
      searchValue: s => (s.next_run_at ? fmtDateTime(s.next_run_at) : ''),
      render: s => <span className="text-xs text-ink-600">{s.next_run_at ? fmtDateTime(s.next_run_at) : '—'}</span>,
    },
    {
      key: 'status', header: 'Last status',
      searchValue: s => s.last_status,
      render: s => (
        <div>
          <div className="flex items-center gap-2">
            {statusBadge(s.last_status)}
            {s.last_run_at > 0 && <span className="text-[11px] text-ink-500">{fmtDateTime(s.last_run_at)}</span>}
          </div>
          {s.last_error && <div className="text-[11px] text-red-700 font-mono mt-1 line-clamp-2" title={s.last_error}>{s.last_error}</div>}
        </div>
      ),
    },
  ];

  return (
    <div className="space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Scheduler</h1>
          <p className="text-sm text-ink-500 mt-1">Recurring ready-made reports delivered via Telegram. All times in UTC (MT5 broker trading day).</p>
        </div>
        <Link to="/schedules/new" className="btn-primary">+ New schedule</Link>
      </div>

      {error && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No schedules yet.</div>
          <Link to="/schedules/new" className="btn-primary">Create the first schedule</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <FolderedCard<ScheduleEntry>
          entityType="schedule"
          rows={items}
          rowKey={s => s.id}
          folderIdOf={s => s.folder_id ?? null}
          rowClassName={s => (s.enabled ? '' : 'opacity-60')}
          onMoved={reload}
          columns={columns}
          onDuplicateFolder={duplicateFolder}
          rowActions={s => (
            <span className="inline-flex items-center gap-0.5">
              <IconButton title="Run now"   onClick={() => onRunNow(s)}><IconPlay /></IconButton>
              <IconButton title="Edit"      onClick={() => nav(`/schedules/${s.id}/edit`)}><IconEdit /></IconButton>
              <IconButton title="Duplicate" onClick={() => onDuplicate(s)}><IconDuplicate /></IconButton>
              <IconButton title="Delete"    danger onClick={() => onDelete(s.id, s.name)}><IconDelete /></IconButton>
            </span>
          )}
        />
      )}

      {/* Settings / help footer — two compact cards below the full-width table */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
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

//--- Inline editor for the Schedule's optional Telegram chat-id override. Idle
//--- state shows `→ <chat>` (or "+ chat id" placeholder when empty); click to
//--- edit, blur/Enter to save, Escape to cancel.
function InlineChatId({ value, onSave }: { value: string; onSave: (v: string) => Promise<void> }) {
  const [editing, setEditing] = useState(false);
  const [text, setText] = useState(value);
  const commit = async () => {
    setEditing(false);
    if (text.trim() === value) return;
    try { await onSave(text.trim()); }
    catch (e: any) { alert(e?.message ?? 'save failed'); setText(value); }
  };
  if (!editing) {
    return (
      <div className="text-[11px] text-ink-500 font-mono mt-0.5 cursor-text hover:bg-ink-50 rounded px-1 -mx-1 inline-block"
           title="Click to set Telegram chat ID"
           onPointerDown={e => e.stopPropagation()}
           onClick={() => { setText(value); setEditing(true); }}>
        {value ? `→ ${value}` : <span className="italic text-ink-400">+ chat id</span>}
      </div>
    );
  }
  return (
    <input className="mt-0.5 text-[11px] font-mono border border-ink-300 rounded px-1 py-0.5"
           style={{ width: 200 }}
           autoFocus
           placeholder="-100… or leave empty for default"
           value={text}
           onPointerDown={e => e.stopPropagation()}
           onChange={e => setText(e.target.value)}
           onBlur={commit}
           onKeyDown={e => {
             if (e.key === 'Enter')  (e.target as HTMLInputElement).blur();
             if (e.key === 'Escape') { setEditing(false); setText(value); }
           }} />
  );
}

//--- Inline <select> for a closed set of options. Always rendered as a real
//--- <select> so the dropdown is one click away, no edit-mode toggle needed.
function InlineSelect<V extends string>({ value, options, onSave }: {
  value: V;
  options: { value: V; label: string }[];
  onSave: (v: V) => Promise<void>;
}) {
  return (
    <select className="text-xs border border-ink-200 bg-white rounded px-1 py-0.5"
            value={value}
            onPointerDown={e => e.stopPropagation()}
            onClick={e => e.stopPropagation()}
            onChange={async e => {
              const next = e.target.value as V;
              if (next === value) return;
              try { await onSave(next); }
              catch (err: any) { alert(err?.message ?? 'save failed'); }
            }}>
      {options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}
    </select>
  );
}
