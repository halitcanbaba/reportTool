import { useEffect, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { ReadyMadeAPI } from '../api/readyMade';
import { TemplatesAPI } from '../api/templates';
import { AccountFiltersAPI } from '../api/accountFilters';
import { ManagersAPI } from '../api/managers';
import { AccountFilterPicker } from '../components/AccountFilterPicker';
import { CompactDateRangeFields } from '../components/CompactDateRangeFields';
import { resolvePreset } from '../lib/dateRange';
import { fmtDateTime, todayLocal } from '../utils/format';
import { copyName } from '../lib/duplicate';
import type { ReadyMadeReport, Template, AccountFilter, Manager, ReadyMadeRunRequest } from '../types';
import { FolderedCard, type FolderedCol } from '../components/FolderedCard';
import { IconButton, IconReadyMade, IconPlay, IconRunWith, IconEdit, IconDuplicate, IconDelete } from '../components/icons';
import { FoldersAPI } from '../api/folders';
import type { Folder } from '../types';

function describeStrategy(rm: ReadyMadeReport): string {
  if (rm.date_strategy === 'fixed') {
    const entries = Object.entries(rm.fixed_dates);
    if (!entries.length) return 'Fixed (no dates)';
    return 'Fixed: ' + entries.map(([k, v]) => `${k}=${v}`).join(', ');
  }
  switch (rm.relative_preset) {
    case 'today':       return 'Relative: today';
    case 'yesterday':   return 'Relative: yesterday';
    case 'last_n_days': return `Relative: last ${rm.relative_n} days`;
    case 'this_week':   return 'Relative: this week';
    case 'last_week':   return 'Relative: last week';
    case 'this_month':  return 'Relative: this month';
    case 'last_month':  return 'Relative: last month';
    default:            return 'Relative';
  }
}

export function ReadyMadeListPage() {
  const nav = useNavigate();
  const [items, setItems] = useState<ReadyMadeReport[]>([]);
  const [templates, setTemplates] = useState<Map<number, Template>>(new Map());
  const [filters, setFilters] = useState<Map<number, AccountFilter>>(new Map());
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [runWith, setRunWith] = useState<ReadyMadeReport | null>(null);

  const reload = async () => {
    setLoading(true);
    try {
      const [rms, tpls, afs] = await Promise.all([
        ReadyMadeAPI.list(),
        TemplatesAPI.list(),
        AccountFiltersAPI.list(),
      ]);
      setItems(rms);
      setTemplates(new Map(tpls.map(t => [t.id, t])));
      setFilters(new Map(afs.map(f => [f.id, f])));
      setError(null);
    } catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete ready-made report "${name}"? Schedules referencing it will block the delete.`)) return;
    try { await ReadyMadeAPI.remove(id); reload(); }
    catch (e: any) { alert(e.message ?? 'delete failed'); }
  };

  const onRun = async (id: number) => {
    try {
      const r = await ReadyMadeAPI.run(id);
      nav(`/jobs/${r.job_id}`);
    } catch (e: any) { alert(e.message ?? 'run failed'); }
  };

  const onDuplicate = async (rm: ReadyMadeReport) => {
    try {
      const full = await ReadyMadeAPI.get(rm.id);
      const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _c; void _u;
      await ReadyMadeAPI.create({ ...rest, name: copyName(rm.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  const duplicateFolder = async (folder: Folder, rows: ReadyMadeReport[]) => {
    const folders = await FoldersAPI.list('ready_made');
    const dup = await FoldersAPI.create({
      entity_type: 'ready_made',
      name: copyName(folder.name, folders.map(f => f.name)),
    });
    const existing = items.map(i => i.name);
    for (const r of rows) {
      const full = await ReadyMadeAPI.get(r.id);
      const { id: _id, created_at: _c, updated_at: _u, folder_id: _f, ...rest } = full as any;
      void _id; void _c; void _u; void _f;
      const created = await ReadyMadeAPI.create({ ...rest, name: copyName(r.name, existing) });
      await FoldersAPI.move('ready_made', (created as any).id, dup.id);
      existing.push((created as any).name ?? '');
    }
    reload();
  };

  //--- Persist a quick-edit change to a Ready-Made row. Sends the full
  //--- payload (templates/account-filter/dates etc.) because the backend's
  //--- PUT isn't partial; we splice the patch into the latest `get` payload.
  const patchReadyMade = async (rm: ReadyMadeReport, patch: Partial<ReadyMadeReport>) => {
    const full = await ReadyMadeAPI.get(rm.id);
    const { id: _id, created_at: _c, updated_at: _u, ...rest } = full as any;
    void _id; void _c; void _u;
    await ReadyMadeAPI.update(rm.id, { ...rest, ...patch });
    reload();
  };

  const columns: FolderedCol<ReadyMadeReport>[] = [
    {
      key: 'name', header: 'Name', searchable: true,
      searchValue: rm => `${rm.name} ${rm.description ?? ''}`,
      render: rm => (
        <div className="flex items-start gap-2">
          <IconReadyMade className="text-ink-500 shrink-0 mt-0.5" />
          <div>
            <div className="font-medium">{rm.name}</div>
            {rm.description && <div className="text-xs text-ink-500">{rm.description}</div>}
          </div>
        </div>
      ),
    },
    {
      key: 'template', header: 'Template', searchable: true,
      searchValue: rm => templates.get(rm.template_id)?.name ?? '',
      render: rm => (
        <select className="text-xs border border-ink-200 bg-white rounded px-1 py-0.5 max-w-[180px]"
                value={rm.template_id}
                title="Change template"
                onPointerDown={e => e.stopPropagation()}
                onClick={e => e.stopPropagation()}
                onChange={async e => {
                  const next = Number(e.target.value);
                  if (next === rm.template_id) return;
                  try { await patchReadyMade(rm, { template_id: next }); }
                  catch (err: any) { alert(err?.message ?? 'save failed'); }
                }}>
          {!templates.has(rm.template_id) && <option value={rm.template_id}>missing #{rm.template_id}</option>}
          {Array.from(templates.values()).map(t => <option key={t.id} value={t.id}>{t.name}</option>)}
        </select>
      ),
    },
    {
      key: 'account_filter', header: 'Account filter', searchable: true,
      searchValue: rm => (rm.account_filter_id ? filters.get(rm.account_filter_id)?.name ?? '' : ''),
      render: rm => (
        <select className="text-xs border border-ink-200 bg-white rounded px-1 py-0.5 max-w-[180px]"
                value={rm.account_filter_id ?? ''}
                title="Change account filter"
                onPointerDown={e => e.stopPropagation()}
                onClick={e => e.stopPropagation()}
                onChange={async e => {
                  const raw = e.target.value;
                  const next = raw === '' ? null : Number(raw);
                  if (next === (rm.account_filter_id ?? null)) return;
                  try { await patchReadyMade(rm, { account_filter_id: next as any }); }
                  catch (err: any) { alert(err?.message ?? 'save failed'); }
                }}>
          <option value="">— manager defaults —</option>
          {Array.from(filters.values()).map(f => <option key={f.id} value={f.id}>{f.name}</option>)}
        </select>
      ),
    },
    {
      key: 'date_strategy', header: 'Date strategy',
      searchValue: rm => describeStrategy(rm),
      render: rm => <span className="text-xs text-ink-700">{describeStrategy(rm)}</span>,
    },
    {
      key: 'updated', header: 'Updated',
      searchValue: rm => fmtDateTime(rm.updated_at),
      render: rm => <span className="text-xs text-ink-500">{fmtDateTime(rm.updated_at)}</span>,
    },
  ];

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Ready-made Reports</h1>
          <p className="text-sm text-ink-500 mt-1">Saved bundles of template + account filter + date strategy. One-click run.</p>
        </div>
        <Link to="/ready-made/new" className="btn-primary">+ New ready-made</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No ready-made reports yet.</div>
          <Link to="/ready-made/new" className="btn-primary">Create the first ready-made</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <FolderedCard<ReadyMadeReport>
          entityType="ready_made"
          rows={items}
          rowKey={rm => rm.id}
          folderIdOf={rm => rm.folder_id ?? null}
          onMoved={reload}
          columns={columns}
          onDuplicateFolder={duplicateFolder}
          rowActions={rm => (
            <span className="inline-flex items-center gap-0.5">
              <IconButton title="Run"        onClick={() => onRun(rm.id)}><IconPlay /></IconButton>
              <IconButton title="Run with…"  onClick={() => setRunWith(rm)}><IconRunWith /></IconButton>
              <IconButton title="Edit"       onClick={() => nav(`/ready-made/${rm.id}/edit`)}><IconEdit /></IconButton>
              <IconButton title="Duplicate"  onClick={() => onDuplicate(rm)}><IconDuplicate /></IconButton>
              <IconButton title="Delete"     danger onClick={() => onDelete(rm.id, rm.name)}><IconDelete /></IconButton>
            </span>
          )}
        />
      )}

      {runWith && (
        <RunWithModal
          rm={runWith}
          template={templates.get(runWith.template_id) ?? null}
          onCancel={() => setRunWith(null)}
          onSubmitted={(job_id) => { setRunWith(null); nav(`/jobs/${job_id}`); }}
        />
      )}
    </div>
  );
}

function RunWithModal({ rm, template, onCancel, onSubmitted }:{
  rm: ReadyMadeReport;
  template: Template | null;
  onCancel: () => void;
  onSubmitted: (job_id: number) => void;
}) {
  const [dates, setDates] = useState<Record<string, string>>(() => {
    const seed: Record<string, string> = {};
    //--- Seed with the ready-made's stored fixed dates if any; otherwise use
    //--- its preset (resolved now) when the template is a 2-param range.
    if (template && template.date_params.length === 2 && rm.date_strategy === 'relative') {
      const key = rm.relative_preset === 'last_n_days'
        ? (rm.relative_n === 30 ? 'last_30' as const : 'last_7' as const)
        : rm.relative_preset;
      try {
        const r = resolvePreset(key as any);
        seed[template.date_params[0]] = r.from;
        seed[template.date_params[1]] = r.to;
      } catch { /* fall through */ }
    }
    template?.date_params.forEach(p => {
      if (!seed[p]) seed[p] = rm.fixed_dates[p] ?? todayLocal();
    });
    return seed;
  });
  const [accountFilterId, setAccountFilterId] = useState<number | null>(rm.account_filter_id);
  const [managerId, setManagerId] = useState<number | null>(null);
  const [managers, setManagers] = useState<Manager[]>([]);
  const [topN, setTopN] = useState<number>(rm.top_n_override || (template?.default_top_n ?? 0));
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    ManagersAPI.list().then(setManagers).catch(() => {});
  }, []);

  const submit = async () => {
    setBusy(true); setErr(null);
    try {
      const payload: ReadyMadeRunRequest = {
        dates,
        account_filter_id: accountFilterId,
        top_n: topN,
      };
      if (managerId != null) payload.manager_id = managerId;
      const r = await ReadyMadeAPI.run(rm.id, payload);
      onSubmitted(r.job_id);
    } catch (e: any) {
      setErr(e.message ?? 'run failed');
      setBusy(false);
    }
  };

  return (
    <div className="fixed inset-0 bg-black/40 flex items-center justify-center z-50">
      <div className="card p-6 w-[640px] max-w-[95vw] space-y-4">
        <div>
          <div className="text-lg font-semibold">Run with overrides</div>
          <div className="text-sm text-ink-500">{rm.name}</div>
        </div>

        {err && <div className="text-red-700 bg-red-50 border border-red-200 rounded px-3 py-2 text-sm font-mono">{err}</div>}

        <div>
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide mb-2">Date range</div>
          {!template && <div className="text-xs text-red-600">Template not loaded — cannot edit dates.</div>}
          {template && <CompactDateRangeFields dateParams={template.date_params} value={dates} onChange={setDates} />}
        </div>

        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Account filter</label>
            <AccountFilterPicker value={accountFilterId} managerId={managerId} onChange={setAccountFilterId} />
          </div>
          <div>
            <label className="label">Manager override (optional)</label>
            <select className="input" value={managerId ?? ''}
                    onChange={e => setManagerId(e.target.value ? Number(e.target.value) : null)}>
              <option value="">— use account filter's manager —</option>
              {managers.map(m => <option key={m.id} value={m.id}>{m.name} ({m.brand})</option>)}
            </select>
          </div>
        </div>

        <div>
          <label className="label">Top N</label>
          <input className="input" type="number" min={0} value={topN} onChange={e => setTopN(Math.max(0, Number(e.target.value || 0)))} />
          <div className="text-xs text-ink-500 mt-1">0 = no limit</div>
        </div>

        <div className="flex justify-end gap-2 pt-2">
          <button className="btn-secondary" onClick={onCancel} disabled={busy}>Cancel</button>
          <button className="btn-primary" onClick={submit} disabled={busy}>{busy ? 'Submitting…' : 'Run'}</button>
        </div>
      </div>
    </div>
  );
}
