import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { ManagersAPI } from '../api/managers';
import type { Manager, ManagerInput, RegexFilters } from '../types';
import { RegexListEditor } from '../components/RegexListEditor';
import { Breadcrumbs } from '../components/Breadcrumbs';

const empty: ManagerInput = {
  name: '', brand: '', region: '',
  server: '', manager_login: 0, password: '',
  group_masks: [], group_regex: '',
  login_min: null, login_max: null,
  active: true,
  regex_filters: { deposit: [], withdrawal: [], writeoff: [], adjustment: [] },
};

export function ManagerEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [form, setForm] = useState<ManagerInput>(empty);
  const [groupMasksRaw, setGroupMasksRaw] = useState('');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    if (!editing) return;
    ManagersAPI.get(Number(id)).then(m => {
      setForm({
        name: m.name, brand: m.brand, region: m.region,
        server: m.server, manager_login: m.manager_login, password: '',
        group_masks: m.group_masks, group_regex: m.group_regex,
        login_min: m.login_min, login_max: m.login_max,
        active: m.active,
        regex_filters: m.regex_filters,
      });
      setGroupMasksRaw(m.group_masks.join(', '));
    });
  }, [editing, id]);

  const update = <K extends keyof ManagerInput>(k: K, v: ManagerInput[K]) =>
    setForm(prev => ({ ...prev, [k]: v }));

  const updateFilters = (k: keyof RegexFilters, list: string[]) =>
    setForm(prev => ({ ...prev, regex_filters: { ...prev.regex_filters, [k]: list } }));

  const save = async () => {
    setBusy(true); setErr(null);
    const masks = groupMasksRaw.split(',').map(s => s.trim()).filter(Boolean);
    const payload: ManagerInput = { ...form, group_masks: masks };
    try {
      if (editing) await ManagersAPI.update(Number(id), payload);
      else         await ManagersAPI.create(payload);
      nav('/managers');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <div>
      <div className="flex items-start justify-between mb-6">
        <div>
          <Breadcrumbs items={[
            { label: 'Managers', to: '/managers' },
            { label: editing ? 'Edit manager' : 'New manager' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit manager' : 'New manager'}</h1>
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/managers')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        </div>
      </div>

      {err && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="card p-5 space-y-4">
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Identity</div>
          <div className="grid grid-cols-2 gap-3">
            <div><label className="label">Name</label><input className="input" value={form.name} onChange={e => update('name', e.target.value)} /></div>
            <div><label className="label">Active</label>
              <select className="input" value={form.active ? '1' : '0'} onChange={e => update('active', e.target.value === '1')}>
                <option value="1">Yes</option><option value="0">No</option>
              </select>
            </div>
            <div><label className="label">Brand</label><input className="input" value={form.brand} onChange={e => update('brand', e.target.value)} placeholder="Trive Invest" /></div>
            <div><label className="label">Region</label><input className="input" value={form.region} onChange={e => update('region', e.target.value)} placeholder="Indonesia" /></div>
          </div>
        </div>

        <div className="card p-5 space-y-4">
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">MT5 connection</div>
          <div className="grid grid-cols-2 gap-3">
            <div><label className="label">Server (host:port)</label><input className="input font-mono text-xs" value={form.server} onChange={e => update('server', e.target.value)} placeholder="live.broker.com:443" /></div>
            <div><label className="label">Manager login</label><input className="input" type="number" value={form.manager_login || ''} onChange={e => update('manager_login', Number(e.target.value || 0))} /></div>
            <div className="col-span-2">
              <label className="label">Password {editing && <span className="text-ink-400 normal-case">(leave blank to keep current)</span>}</label>
              <input className="input" type="password" value={form.password ?? ''} onChange={e => update('password', e.target.value)} />
            </div>
          </div>
        </div>

        <div className="card p-5 space-y-4 lg:col-span-2">
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">User filters</div>
          <div>
            <label className="label">Group masks (MT5 wildcard, comma-separated)</label>
            <input className="input font-mono text-xs" value={groupMasksRaw}
                   onChange={e => setGroupMasksRaw(e.target.value)}
                   placeholder={'real\\\\*, demo\\\\*'} />
            <div className="text-xs text-ink-500 mt-1">e.g. <code className="font-mono">real\GKB\*</code>, <code className="font-mono">real\*</code>, <code className="font-mono">*</code></div>
          </div>
          <div>
            <label className="label">Group regex post-filter (optional, ECMAScript)</label>
            <input className="input font-mono text-xs" value={form.group_regex} onChange={e => update('group_regex', e.target.value)} placeholder="^real\\\\.+$" />
          </div>
          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="label">Login min (optional)</label>
              <input className="input" type="number" value={form.login_min ?? ''} onChange={e => update('login_min', e.target.value ? Number(e.target.value) : null)} />
            </div>
            <div>
              <label className="label">Login max (optional)</label>
              <input className="input" type="number" value={form.login_max ?? ''} onChange={e => update('login_max', e.target.value ? Number(e.target.value) : null)} />
            </div>
          </div>
        </div>

        <div className="lg:col-span-2 space-y-4">
          <div className="flex items-center justify-between">
            <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Net deposit comment filters</div>
            <div className="text-[11px] text-ink-500 italic">
              Evaluation order (first match wins): Deposit → Withdrawal → Writeoff → Adjustment
            </div>
          </div>
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <RegexListEditor
              label="Deposit"
              hint="DEAL_BALANCE with profit > 0 matched here counts as deposit."
              value={form.regex_filters.deposit}
              onChange={v => updateFilters('deposit', v)}
              usedBy={['sum_deposit(F,T)', 'count_deposits(F,T)']}
            />
            <RegexListEditor
              label="Withdrawal"
              hint="DEAL_BALANCE with profit < 0 matched here counts as withdrawal."
              value={form.regex_filters.withdrawal}
              onChange={v => updateFilters('withdrawal', v)}
              usedBy={['sum_withdrawal(F,T)', 'count_withdrawals(F,T)']}
              extraNote="Result is kept negative (subtracted from net deposit)."
            />
            <RegexListEditor
              label="Balance writeoff"
              hint="DEAL_BALANCE matched here goes to Balance Writeoff column."
              value={form.regex_filters.writeoff}
              onChange={v => updateFilters('writeoff', v)}
              usedBy={['sum_writeoff(F,T)']}
            />
            <RegexListEditor
              label="Trade adjustment"
              hint="DEAL_BALANCE matched here + DEAL_CORRECTION → Trade Adjustments."
              value={form.regex_filters.adjustment}
              onChange={v => updateFilters('adjustment', v)}
              usedBy={['sum_adjustment(F,T)']}
              extraNote="DEAL_CORRECTION deals are always classified as adjustments regardless of comment."
            />
          </div>
        </div>
      </div>
    </div>
  );
}
