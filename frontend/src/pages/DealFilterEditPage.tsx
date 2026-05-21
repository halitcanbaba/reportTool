import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { DealFiltersAPI } from '../api/dealFilters';
import { FieldsAPI } from '../api/fields';
import { PredicateEditor } from '../components/PredicateEditor';
import { DealFilterPreview } from '../components/DealFilterPreview';
import { Breadcrumbs } from '../components/Breadcrumbs';
import type { DealFilter, FieldCatalog, Predicate } from '../types';

export function DealFilterEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [catalog, setCatalog] = useState<FieldCatalog | null>(null);
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [predicate, setPredicate] = useState<Predicate | null>(null);

  const [busy, setBusy]   = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    FieldsAPI.catalog().then(setCatalog).catch(e => setError(e.message ?? 'failed to load fields'));
  }, []);

  useEffect(() => {
    if (!editing) return;
    DealFiltersAPI.get(Number(id)).then((f: DealFilter) => {
      setName(f.name);
      setDescription(f.description);
      setPredicate(f.predicate);
    }).catch(e => setError(e.message ?? 'failed to load'));
  }, [editing, id]);

  const onSave = async () => {
    if (!name.trim()) { setError('Name is required.'); return; }
    if (!predicate)   { setError('Define at least one predicate condition.'); return; }
    setBusy(true); setError(null);
    try {
      if (editing) await DealFiltersAPI.update(Number(id), { name, description, predicate });
      else         await DealFiltersAPI.create({ name, description, predicate });
      nav('/deal-filters');
    } catch (e: any) {
      setError(e.message ?? 'save failed');
    } finally {
      setBusy(false);
    }
  };

  if (!catalog) return <div className="text-sm text-ink-400">Loading field catalog…</div>;
  const dealFilterable = catalog.filterable_by_source?.deal ?? [];

  return (
    <div className="space-y-4">
      <div>
        <Breadcrumbs items={[
          { label: 'Deal filters', to: '/deal-filters' },
          { label: editing ? `Edit ${name || '#' + id}` : 'New deal filter' },
        ]} />
        <h1 className="text-2xl font-semibold text-ink-900">
          {editing ? `Edit ${name || `Deal filter #${id}`}` : 'New deal filter'}
        </h1>
        <p className="text-sm text-ink-500 mt-1">
          Define the predicate that picks rows out of the deal stream. Use the preview below to verify your rule
          against real cash-flow deals before saving.
        </p>
      </div>

      {error && <div className="card p-3 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}

      <div className="card p-5 space-y-3">
        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          <div className="md:col-span-1">
            <label className="label">Name</label>
            <input className="input" value={name} onChange={e => setName(e.target.value)}
                   placeholder="e.g. Cash deposit (non-promo)" />
          </div>
          <div className="md:col-span-2">
            <label className="label">Description</label>
            <input className="input" value={description} onChange={e => setDescription(e.target.value)}
                   placeholder="Optional: when to use this filter, who it's for, etc." />
          </div>
        </div>
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Predicate</div>
        <p className="text-xs text-ink-500">
          Conditions are AND-combined at the top level. Build trees with AND/OR/NOT to express
          "DEAL_BALANCE AND comment contains BANK" style rules.
        </p>
        <PredicateEditor
          source="deal"
          filterable={dealFilterable}
          predicate={predicate}
          onChange={setPredicate}
        />
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Preview</div>
        <p className="text-xs text-ink-500">
          Pick a manager + (optional) account filter + date range, then click "Preview matches".
          Rows matched by the predicate above are highlighted green.
        </p>
        <DealFilterPreview predicate={predicate} />
      </div>

      <div className="flex justify-end gap-2">
        <button type="button" className="btn-secondary" onClick={() => nav('/deal-filters')} disabled={busy}>
          Cancel
        </button>
        <button type="button" className="btn-primary" onClick={onSave} disabled={busy || !name.trim() || !predicate}>
          {busy ? 'Saving…' : editing ? 'Save changes' : 'Create deal filter'}
        </button>
      </div>
    </div>
  );
}
