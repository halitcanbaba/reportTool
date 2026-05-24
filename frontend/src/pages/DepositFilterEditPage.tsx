import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { DepositFiltersAPI } from '../api/depositFilters';
import { FieldsAPI } from '../api/fields';
import { PredicateEditor } from '../components/PredicateEditor';
import { DepositFilterPreview } from '../components/DepositFilterPreview';
import { Breadcrumbs } from '../components/Breadcrumbs';
import type { DepositFilter, DepositFilterBucket, FieldCatalog, Predicate } from '../types';

//--- One bucket card: key + label inputs + PredicateEditor + remove button.
function BucketCard({
  index, bucket, dealFilterable, onChange, onRemove,
}: {
  index: number;
  bucket: DepositFilterBucket;
  dealFilterable: FieldCatalog['filterable_by_source'] extends infer T ? any : never;
  onChange: (b: DepositFilterBucket) => void;
  onRemove: () => void;
}) {
  return (
    <div className="border border-ink-200 rounded p-4 space-y-3 bg-white">
      <div className="flex items-center gap-3 flex-wrap">
        <div className="text-xs text-ink-400 font-mono">#{index + 1}</div>
        <div className="flex-1 min-w-[160px]">
          <label className="label">Key (machine name)</label>
          <input
            className="input"
            value={bucket.key}
            onChange={e => onChange({ ...bucket, key: e.target.value.replace(/\s+/g, '_').toLowerCase() })}
            placeholder="cash_deposit"
          />
        </div>
        <div className="flex-1 min-w-[160px]">
          <label className="label">Label (display)</label>
          <input
            className="input"
            value={bucket.label}
            onChange={e => onChange({ ...bucket, label: e.target.value })}
            placeholder="Cash Deposit"
          />
        </div>
        <button type="button" onClick={onRemove}
                className="btn-secondary text-xs text-red-600 self-end">
          × Remove
        </button>
      </div>
      <div>
        <div className="text-[11px] text-ink-500 mb-1 uppercase tracking-wide font-medium">
          Predicate over deal source
        </div>
        <PredicateEditor
          source="deal"
          filterable={dealFilterable}
          predicate={bucket.predicate ?? null}
          onChange={p => onChange({ ...bucket, predicate: p ?? bucket.predicate })}
        />
      </div>
    </div>
  );
}

export function DepositFilterEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [catalog, setCatalog] = useState<FieldCatalog | null>(null);
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [buckets, setBuckets] = useState<DepositFilterBucket[]>([]);

  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    FieldsAPI.catalog().then(setCatalog).catch(e => setError(e.message ?? 'failed to load fields'));
  }, []);

  useEffect(() => {
    if (!editing) return;
    DepositFiltersAPI.get(Number(id)).then((f: DepositFilter) => {
      setName(f.name);
      setDescription(f.description);
      setBuckets(f.buckets ?? []);
    }).catch(e => setError(e.message ?? 'failed to load'));
  }, [editing, id]);

  const addBucket = () => {
    const n = buckets.length + 1;
    //--- Default predicate: a sane "always-false" starter so the user can
    //--- immediately edit it; backend rejects buckets with null predicate
    //--- so we never persist this placeholder.
    const placeholder: Predicate = {
      kind: 'cmp', field: 'action', op: 'eq', value: 0,
    };
    setBuckets([
      ...buckets,
      { key: `bucket_${n}`, label: `Bucket ${n}`, predicate: placeholder },
    ]);
  };

  const setBucket = (idx: number, b: DepositFilterBucket) =>
    setBuckets(prev => prev.map((x, i) => i === idx ? b : x));

  const removeBucket = (idx: number) =>
    setBuckets(prev => prev.filter((_, i) => i !== idx));

  const onSave = async () => {
    if (!name.trim()) { setError('Name is required.'); return; }
    if (buckets.length === 0) { setError('Add at least one bucket.'); return; }
    const keys = new Set<string>();
    for (const b of buckets) {
      if (!b.key.trim())  { setError('Every bucket needs a key.'); return; }
      if (keys.has(b.key)) { setError(`Duplicate bucket key: ${b.key}`); return; }
      keys.add(b.key);
      if (!b.predicate)   { setError(`Bucket "${b.key}" needs a predicate.`); return; }
    }
    setBusy(true); setError(null);
    try {
      const payload = { name, description, buckets };
      if (editing) await DepositFiltersAPI.update(Number(id), payload);
      else         await DepositFiltersAPI.create(payload);
      nav('/deposit-filters');
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
          { label: 'Deposit filters', to: '/deposit-filters' },
          { label: editing ? `Edit ${name || '#' + id}` : 'New deposit filter' },
        ]} />
        <h1 className="text-2xl font-semibold text-ink-900">
          {editing ? `Edit ${name || `Deposit filter #${id}`}` : 'New deposit filter'}
        </h1>
        <p className="text-sm text-ink-500 mt-1">
          Define one bucket per cash-flow category for this broker (cash deposit, promotion, rebate, …).
          Bind this preset to a ready-made report so <span className="font-mono">sum_deposit_amount</span> /
          <span className="font-mono"> count_deposits</span> aggregator fields can resolve bucket keys at run time.
        </p>
      </div>

      {error && <div className="card p-3 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}

      <div className="card p-5 space-y-3">
        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          <div className="md:col-span-1">
            <label className="label">Name</label>
            <input className="input" value={name} onChange={e => setName(e.target.value)}
                   placeholder="Trive Conventions" />
          </div>
          <div className="md:col-span-2">
            <label className="label">Description</label>
            <input className="input" value={description} onChange={e => setDescription(e.target.value)}
                   placeholder="Optional: who this is for, when to apply, etc." />
          </div>
        </div>
      </div>

      <div className="space-y-3">
        <div className="flex items-center justify-between">
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Buckets</div>
          <button type="button" className="btn-secondary text-sm" onClick={addBucket}>+ Add bucket</button>
        </div>
        {buckets.length === 0 && (
          <div className="card p-8 text-center text-ink-400 text-sm">
            No buckets yet. Add at least one — each bucket gives the new <span className="font-mono">sum_deposit_amount(bucket, …)</span>
            field one rule to resolve.
          </div>
        )}
        {buckets.map((b, i) => (
          <BucketCard
            key={i}
            index={i}
            bucket={b}
            dealFilterable={dealFilterable}
            onChange={nb => setBucket(i, nb)}
            onRemove={() => removeBucket(i)}
          />
        ))}
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Preview</div>
        <p className="text-xs text-ink-500">
          Pick a manager + (optional) account filter + date range, then click "Preview matches". Every cash-flow
          deal in the window is tagged with the bucket key(s) whose predicate matched.
        </p>
        <DepositFilterPreview buckets={buckets} />
      </div>

      <div className="flex justify-end gap-2">
        <button type="button" className="btn-secondary" onClick={() => nav('/deposit-filters')} disabled={busy}>
          Cancel
        </button>
        <button type="button" className="btn-primary" onClick={onSave}
                disabled={busy || !name.trim() || buckets.length === 0}>
          {busy ? 'Saving…' : editing ? 'Save changes' : 'Create deposit filter'}
        </button>
      </div>
    </div>
  );
}
