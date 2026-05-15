import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { AccountFiltersAPI } from '../api/accountFilters';
import { ManagersAPI } from '../api/managers';
import { FieldsAPI } from '../api/fields';
import type { AccountFilterInput, FieldCatalog, Manager, Predicate } from '../types';
import { GroupPicker } from '../components/GroupRegexInput';
import { AccountFilterPreview } from '../components/AccountFilterPreview';
import { PredicateEditor } from '../components/PredicateEditor';
import { Breadcrumbs } from '../components/Breadcrumbs';

const empty: AccountFilterInput = {
  name: '', description: '',
  group_masks: [], group_regex: '',
  login_min: null, login_max: null,
  manager_id: null,
  user_predicate: null,
};

export function AccountFilterEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [form, setForm] = useState<AccountFilterInput>(empty);
  const [managers, setManagers] = useState<Manager[]>([]);
  const [catalog, setCatalog] = useState<FieldCatalog | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    ManagersAPI.list().then(setManagers).catch(() => {});
    FieldsAPI.catalog().then(setCatalog).catch(() => {});
  }, []);

  useEffect(() => {
    if (!editing) return;
    AccountFiltersAPI.get(Number(id)).then(f => {
      //--- Group selection is the inline tree picker over `group_masks`.
      //--- `group_regex` and `login_min/max` no longer have a UI here and get
      //--- cleared on next save (predicate editor below covers login filtering).
      setForm({
        name: f.name, description: f.description,
        group_masks: f.group_masks, group_regex: '',
        login_min: null, login_max: null,
        manager_id: f.manager_id,
        user_predicate: f.user_predicate ?? null,
      });
    });
  }, [editing, id]);

  const update = <K extends keyof AccountFilterInput>(k: K, v: AccountFilterInput[K]) =>
    setForm(prev => ({ ...prev, [k]: v }));

  const save = async () => {
    setBusy(true); setErr(null);
    try {
      if (editing) await AccountFiltersAPI.update(Number(id), form);
      else         await AccountFiltersAPI.create(form);
      nav('/account-filters');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  const userFilterable = catalog?.filterable_by_source?.user ?? [];

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'Account filters', to: '/account-filters' },
            { label: editing ? 'Edit account filter' : 'New account filter' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit account filter' : 'New account filter'}</h1>
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/account-filters')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        </div>
      </div>

      {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Identity</div>
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Name</label>
            <input className="input" value={form.name} onChange={e => update('name', e.target.value)} placeholder="Indonesia Live" />
          </div>
          <div>
            <label className="label">Manager</label>
            <select className="input" value={form.manager_id ?? ''}
                    onChange={e => update('manager_id', e.target.value ? Number(e.target.value) : null)}>
              <option value="">— generic (any manager) —</option>
              {managers.map(m => <option key={m.id} value={m.id}>{m.name} ({m.brand})</option>)}
            </select>
            <div className="text-xs text-ink-500 mt-1">
              Required for group discovery and preview. Generic filters can be used across managers but won't list groups.
            </div>
          </div>
        </div>
        <div>
          <label className="label">Description</label>
          <input className="input" value={form.description} onChange={e => update('description', e.target.value)} />
        </div>
      </div>

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Groups</div>
        <GroupPicker managerId={form.manager_id ?? null}
                     value={form.group_masks}
                     onChange={v => update('group_masks', v)} />
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Additional account filters (optional)</div>
        <div className="text-[11px] text-ink-500">
          Per-row predicates on IMTUser fields (comment, agent, zip, country, …). Applied after group masks each time the report runs.
        </div>
        {catalog ? (
          <PredicateEditor
            source="user"
            filterable={userFilterable}
            predicate={form.user_predicate ?? null}
            onChange={(p: Predicate | null) => update('user_predicate', p)}
          />
        ) : (
          <div className="text-xs text-ink-400">Loading field catalog…</div>
        )}
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Preview</div>
        <AccountFilterPreview
          managerId={form.manager_id ?? null}
          groupMasks={form.group_masks}
          groupRegex={form.group_regex}
          loginMin={form.login_min}
          loginMax={form.login_max}
          userPredicate={form.user_predicate ?? null}
        />
      </div>
    </div>
  );
}
