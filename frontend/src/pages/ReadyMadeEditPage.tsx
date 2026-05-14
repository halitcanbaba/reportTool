import { useEffect, useMemo, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { ReadyMadeAPI } from '../api/readyMade';
import { TemplatesAPI } from '../api/templates';
import { AccountFiltersAPI } from '../api/accountFilters';
import { DateStrategyPicker } from '../components/DateStrategyPicker';
import { Breadcrumbs } from '../components/Breadcrumbs';
import type { ReadyMadeReportInput, Template, AccountFilter } from '../types';

const empty: ReadyMadeReportInput = {
  name: '',
  description: '',
  template_id: 0,
  account_filter_id: null,
  date_strategy: 'relative',
  fixed_dates: {},
  relative_preset: 'yesterday',
  relative_n: 7,
  top_n_override: 0,
};

export function ReadyMadeEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [form, setForm] = useState<ReadyMadeReportInput>(empty);
  const [templates, setTemplates] = useState<Template[]>([]);
  const [filters, setFilters] = useState<AccountFilter[]>([]);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    TemplatesAPI.list().then(setTemplates).catch(() => {});
    AccountFiltersAPI.list().then(setFilters).catch(() => {});
  }, []);

  useEffect(() => {
    if (!editing) return;
    ReadyMadeAPI.get(Number(id)).then(rm => {
      setForm({
        name: rm.name,
        description: rm.description,
        template_id: rm.template_id,
        account_filter_id: rm.account_filter_id,
        date_strategy: rm.date_strategy,
        fixed_dates: rm.fixed_dates ?? {},
        relative_preset: rm.relative_preset,
        relative_n: rm.relative_n,
        top_n_override: rm.top_n_override,
      });
    });
  }, [editing, id]);

  const selectedTemplate = useMemo(
    () => templates.find(t => t.id === form.template_id) ?? null,
    [templates, form.template_id]
  );

  const update = <K extends keyof ReadyMadeReportInput>(k: K, v: ReadyMadeReportInput[K]) =>
    setForm(prev => ({ ...prev, [k]: v }));

  const save = async () => {
    if (!form.name.trim()) { setErr('name required'); return; }
    if (!form.template_id) { setErr('select a template'); return; }
    setBusy(true); setErr(null);
    try {
      if (editing) await ReadyMadeAPI.update(Number(id), form);
      else         await ReadyMadeAPI.create(form);
      nav('/ready-made');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'Ready-made', to: '/ready-made' },
            { label: editing ? 'Edit ready-made report' : 'New ready-made report' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit ready-made report' : 'New ready-made report'}</h1>
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/ready-made')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        </div>
      </div>

      {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Identity</div>
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Name</label>
            <input className="input" value={form.name} onChange={e => update('name', e.target.value)} placeholder="Daily Top Winners — Indonesia" />
          </div>
          <div>
            <label className="label">Description</label>
            <input className="input" value={form.description} onChange={e => update('description', e.target.value)} />
          </div>
        </div>
      </div>

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Bundle</div>
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Template</label>
            <select className="input" value={form.template_id || ''}
                    onChange={e => update('template_id', Number(e.target.value))}>
              <option value="">— select template —</option>
              {templates.map(t => <option key={t.id} value={t.id}>{t.name}</option>)}
            </select>
            {selectedTemplate && (
              <div className="text-xs text-ink-500 mt-1">
                date params: <span className="font-mono">{selectedTemplate.date_params.join(', ') || 'none'}</span> · default top {selectedTemplate.default_top_n}
              </div>
            )}
          </div>
          <div>
            <label className="label">Account filter (optional)</label>
            <select className="input" value={form.account_filter_id ?? ''}
                    onChange={e => update('account_filter_id', e.target.value ? Number(e.target.value) : null)}>
              <option value="">— manager defaults —</option>
              {filters.map(f => (
                <option key={f.id} value={f.id}>{f.name}{f.manager_id ? ' (bound)' : ''}</option>
              ))}
            </select>
            <div className="text-xs text-ink-500 mt-1">
              Bound filters carry their own manager; generic filters require a manager override at run time.
            </div>
          </div>
        </div>
        <div>
          <label className="label">Top N override</label>
          <input className="input" type="number" min={0} value={form.top_n_override}
                 onChange={e => update('top_n_override', Math.max(0, Number(e.target.value || 0)))} />
          <div className="text-xs text-ink-500 mt-1">0 = use template default ({selectedTemplate?.default_top_n ?? '—'})</div>
        </div>
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Date range</div>
        <DateStrategyPicker
          template={selectedTemplate}
          value={{
            date_strategy:   form.date_strategy,
            fixed_dates:     form.fixed_dates,
            relative_preset: form.relative_preset,
            relative_n:      form.relative_n,
          }}
          onChange={(next) =>
            setForm(prev => ({
              ...prev,
              date_strategy:   next.date_strategy,
              fixed_dates:     next.fixed_dates,
              relative_preset: next.relative_preset,
              relative_n:      next.relative_n,
            }))
          }
        />
      </div>
    </div>
  );
}
