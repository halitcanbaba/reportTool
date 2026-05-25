import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { SchedulesAPI } from '../api/schedules';
import { ReadyMadeAPI } from '../api/readyMade';
import { Breadcrumbs } from '../components/Breadcrumbs';
import type { ScheduleEntryInput, ReadyMadeReport, ScheduleFrequency, ScheduleDeliveryFormat } from '../types';

const empty: ScheduleEntryInput = {
  name: '',
  ready_made_id: 0,
  frequency: 'daily',
  time_hour: 8,
  time_minute: 0,
  day_of_week: 1,
  day_of_month: 1,
  every_n_hours: 1,
  hours: [],
  days_of_week: [],
  telegram_chat_id: '',
  delivery_format: 'csv',
  enabled: true,
};

const DOW = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
const DOW_SHORT = ['S','M','T','W','T','F','S'];

export function ScheduleEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [form, setForm] = useState<ScheduleEntryInput>(empty);
  const [readyMades, setReadyMades] = useState<ReadyMadeReport[]>([]);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    ReadyMadeAPI.list().then(setReadyMades).catch(() => {});
  }, []);

  useEffect(() => {
    if (!editing) return;
    SchedulesAPI.get(Number(id)).then(s => {
      setForm({
        name: s.name,
        ready_made_id: s.ready_made_id,
        frequency: s.frequency,
        time_hour: s.time_hour,
        time_minute: s.time_minute,
        day_of_week: s.day_of_week,
        day_of_month: s.day_of_month,
        every_n_hours: s.every_n_hours,
        hours: s.hours ?? [],
        days_of_week: s.days_of_week ?? [],
        telegram_chat_id: s.telegram_chat_id,
        delivery_format: s.delivery_format ?? 'csv',
        enabled: s.enabled,
      });
    });
  }, [editing, id]);

  const update = <K extends keyof ScheduleEntryInput>(k: K, v: ScheduleEntryInput[K]) =>
    setForm(prev => ({ ...prev, [k]: v }));

  const save = async () => {
    if (!form.name.trim()) { setErr('name required'); return; }
    if (!form.ready_made_id) { setErr('select a ready-made report'); return; }
    setBusy(true); setErr(null);
    try {
      if (editing) await SchedulesAPI.update(Number(id), form);
      else         await SchedulesAPI.create(form);
      nav('/schedules');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  const showTime = form.frequency !== 'hourly';

  const toggleHour = (h: number) => {
    const set = new Set(form.hours);
    if (set.has(h)) set.delete(h); else set.add(h);
    update('hours', Array.from(set).sort((a, b) => a - b));
  };
  const toggleDow = (d: number) => {
    const set = new Set(form.days_of_week);
    if (set.has(d)) set.delete(d); else set.add(d);
    update('days_of_week', Array.from(set).sort((a, b) => a - b));
  };
  const usingSpecificHours = form.hours.length > 0;

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'Scheduler', to: '/schedules' },
            { label: editing ? 'Edit schedule' : 'New schedule' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit schedule' : 'New schedule'}</h1>
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/schedules')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        </div>
      </div>

      {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Identity</div>
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Name</label>
            <input className="input" value={form.name} onChange={e => update('name', e.target.value)} placeholder="Daily Indonesia 08:00" />
          </div>
          <div>
            <label className="label">Ready-made report</label>
            <select className="input" value={form.ready_made_id || ''}
                    onChange={e => update('ready_made_id', Number(e.target.value))}>
              <option value="">— select —</option>
              {readyMades.map(rm => <option key={rm.id} value={rm.id}>{rm.name}</option>)}
            </select>
          </div>
        </div>

        <label className="flex items-center gap-2 text-sm cursor-pointer">
          <input type="checkbox" checked={form.enabled} onChange={e => update('enabled', e.target.checked)} />
          Enabled
        </label>
      </div>

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Recurrence (UTC)</div>

        <div>
          <label className="label">Frequency</label>
          <select className="input" value={form.frequency}
                  onChange={e => update('frequency', e.target.value as ScheduleFrequency)}>
            <option value="daily">Daily</option>
            <option value="weekly">Weekly</option>
            <option value="monthly">Monthly</option>
            <option value="hourly">Hourly</option>
          </select>
        </div>

        {form.frequency === 'weekly' && (
          <div>
            <label className="label">Day of week</label>
            <select className="input" value={form.day_of_week}
                    onChange={e => update('day_of_week', Number(e.target.value))}>
              {DOW.map((d, i) => <option key={i} value={i}>{d}</option>)}
            </select>
          </div>
        )}

        {form.frequency === 'monthly' && (
          <div>
            <label className="label">Day of month (1–28)</label>
            <input className="input" type="number" min={1} max={28}
                   value={form.day_of_month}
                   onChange={e => update('day_of_month', Math.min(28, Math.max(1, Number(e.target.value || 1))))} />
          </div>
        )}

        {form.frequency === 'hourly' && (
          <div className="space-y-3">
            <div>
              <label className="label">Hours of day</label>
              <div className="grid grid-cols-12 gap-1.5">
                {Array.from({ length: 24 }, (_, h) => {
                  const on = form.hours.includes(h);
                  return (
                    <button key={h} type="button" onClick={() => toggleHour(h)}
                            className={`h-8 rounded-md text-xs font-mono border transition-colors ${
                              on
                                ? 'bg-amber-100 border-amber-400 text-amber-900'
                                : 'bg-white border-ink-200 text-ink-700 hover:bg-ink-50'
                            }`}>
                      {String(h).padStart(2, '0')}
                    </button>
                  );
                })}
              </div>
              <div className="text-[11px] text-ink-500 mt-1">
                Click hours to enable specific firing times. Leave all unselected to use the legacy "every N hours" fallback below.
              </div>
            </div>
            <div className={usingSpecificHours ? 'opacity-50 pointer-events-none' : ''}>
              <label className="label">Every N hours (fallback)</label>
              <input className="input w-32" type="number" min={1} max={24}
                     value={form.every_n_hours}
                     onChange={e => update('every_n_hours', Math.min(24, Math.max(1, Number(e.target.value || 1))))} />
            </div>
            <div>
              <label className="label">Minute (0–59)</label>
              <input className="input w-32" type="number" min={0} max={59}
                     value={form.time_minute}
                     onChange={e => update('time_minute', Math.min(59, Math.max(0, Number(e.target.value || 0))))} />
            </div>
          </div>
        )}

        {showTime && (
          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="label">Hour (0–23)</label>
              <input className="input" type="number" min={0} max={23}
                     value={form.time_hour}
                     onChange={e => update('time_hour', Math.min(23, Math.max(0, Number(e.target.value || 0))))} />
            </div>
            <div>
              <label className="label">Minute (0–59)</label>
              <input className="input" type="number" min={0} max={59}
                     value={form.time_minute}
                     onChange={e => update('time_minute', Math.min(59, Math.max(0, Number(e.target.value || 0))))} />
            </div>
          </div>
        )}

        {(form.frequency === 'daily' || form.frequency === 'hourly') && (
          <div>
            <label className="label">Days of week</label>
            <div className="flex gap-1.5">
              {DOW_SHORT.map((s, i) => {
                const on = form.days_of_week.includes(i);
                return (
                  <button key={i} type="button" onClick={() => toggleDow(i)}
                          title={DOW[i]}
                          className={`h-8 w-9 rounded-md text-xs font-semibold border transition-colors ${
                            on
                              ? 'bg-amber-100 border-amber-400 text-amber-900'
                              : 'bg-white border-ink-200 text-ink-700 hover:bg-ink-50'
                          }`}>
                    {s}
                  </button>
                );
              })}
            </div>
            <div className="text-[11px] text-ink-500 mt-1">
              Leave all unselected to fire every day.
            </div>
          </div>
        )}
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Delivery</div>
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Send as</label>
            <select className="input"
                    value={form.delivery_format}
                    onChange={e => update('delivery_format', e.target.value as ScheduleDeliveryFormat)}>
              <option value="csv">CSV file (raw, full dataset)</option>
              <option value="xlsx">XLSX spreadsheet (typed cells)</option>
              <option value="pdf">PDF document (tabular print)</option>
              <option value="image">Screenshot PNG (headless Chrome)</option>
              <option value="text">Text summary (HTML formatted)</option>
            </select>
            <div className="text-[11px] text-ink-500 mt-1">
              <span className="font-mono">image</span> needs Chrome / Edge on the server and a working
              public URL; falls back to a text notice if either is missing.
            </div>
          </div>
          <div>
            <label className="label">Telegram chat ID (override)</label>
            <input className="input font-mono text-xs" value={form.telegram_chat_id}
                   onChange={e => update('telegram_chat_id', e.target.value)}
                   placeholder="leave blank to use default" />
            <div className="text-[11px] text-ink-500 mt-1">
              Channels start with <code>-100…</code>. Empty = use the global default.
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
