import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { TemplatesAPI } from '../api/templates';
import { ManagersAPI } from '../api/managers';
import { ReportsAPI } from '../api/reports';
import { DepositFiltersAPI } from '../api/depositFilters';
import { AccountFilterPicker } from '../components/AccountFilterPicker';
import { DateParamsForm } from '../components/DateParamsForm';
import { Breadcrumbs } from '../components/Breadcrumbs';
import { resolvePreset } from '../lib/dateRange';
import type { DepositFilter, Manager, RunReportRequest, Template } from '../types';

export function RunReportPage() {
  const { id } = useParams();
  const nav = useNavigate();
  const [tpl, setTpl] = useState<Template | null>(null);
  const [managers, setManagers] = useState<Manager[]>([]);
  const [managerId, setManagerId] = useState<number | null>(null);
  const [accountFilterId, setAccountFilterId] = useState<number | null>(null);
  const [depositFilters, setDepositFilters] = useState<DepositFilter[]>([]);
  const [depositFilterId, setDepositFilterId] = useState<number | null>(null);
  const [dates, setDates] = useState<Record<string, string>>({});
  const [topN, setTopN] = useState(0);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    if (id == null) return;
    TemplatesAPI.get(Number(id)).then(t => {
      setTpl(t);
      setTopN(t.default_top_n);
      //--- Seed with the "Last 30 days" preset for 2-param templates;
      //--- for other arities, leave blank for the user to fill.
      const d: Record<string, string> = {};
      if (t.date_params.length === 2) {
        const r = resolvePreset('last_30');
        d[t.date_params[0]] = r.from;
        d[t.date_params[1]] = r.to;
      }
      setDates(d);
    });
    ManagersAPI.list().then(setManagers).then(() => {});
    DepositFiltersAPI.list().then(setDepositFilters).catch(() => {});
  }, [id]);

  useEffect(() => {
    if (managers.length && managerId == null) setManagerId(managers[0].id);
  }, [managers, managerId]);

  if (!tpl) return <div className="text-sm text-ink-400">Loading template…</div>;

  const submit = async () => {
    if (managerId == null) { setErr('select a manager'); return; }
    setBusy(true); setErr(null);
    const req: RunReportRequest = {
      template_id: tpl.id,
      manager_id:  managerId,
      account_filter_id: accountFilterId ?? undefined,
      deposit_filter_id: depositFilterId ?? undefined,
      dates,
      top_n: topN,
    };
    try {
      const r = await ReportsAPI.run(req);
      nav(`/jobs/${r.job_id}`);
    } catch (e: any) {
      setErr(e.message ?? 'run failed');
    } finally { setBusy(false); }
  };

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'Templates', to: '/templates' },
            { label: tpl.name, to: `/templates/${tpl.id}/edit` },
            { label: 'Run' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">Run: {tpl.name}</h1>
          {tpl.description && <p className="text-sm text-ink-500 mt-1">{tpl.description}</p>}
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/templates')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={submit}>{busy ? 'Submitting…' : 'Run'}</button>
        </div>
      </div>

      {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="card p-5 space-y-4">
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Manager</label>
            <select className="input" value={managerId ?? ''} onChange={e => setManagerId(Number(e.target.value))}>
              {managers.map(m => <option key={m.id} value={m.id}>{m.name} ({m.brand})</option>)}
            </select>
          </div>
          <div>
            <label className="label">Account filter</label>
            <AccountFilterPicker value={accountFilterId} managerId={managerId} onChange={setAccountFilterId} />
          </div>
        </div>

        <div>
          <label className="label">Deposit filter (optional)</label>
          <select className="input" value={depositFilterId ?? ''}
                  onChange={e => setDepositFilterId(e.target.value ? Number(e.target.value) : null)}>
            <option value="">— none —</option>
            {depositFilters.map(f => (
              <option key={f.id} value={f.id}>{f.name} ({f.buckets.length} bucket{f.buckets.length === 1 ? '' : 's'})</option>
            ))}
          </select>
          <div className="text-xs text-ink-500 mt-1">
            Required for templates that reference <span className="font-mono">sum_deposit_amount</span> /
            <span className="font-mono"> count_deposits</span>.
          </div>
        </div>

        <div>
          <label className="label">Top N</label>
          <input className="input" type="number" min={0} value={topN} onChange={e => setTopN(Math.max(0, Number(e.target.value || 0)))} />
          <div className="text-xs text-ink-500 mt-1">0 = no limit</div>
        </div>

        <div>
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide mb-2">Date range</div>
          <DateParamsForm dateParams={tpl.date_params} value={dates} onChange={setDates} />
        </div>
      </div>
    </div>
  );
}
