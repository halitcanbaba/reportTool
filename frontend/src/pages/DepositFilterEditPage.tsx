import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { DepositFiltersAPI } from '../api/depositFilters';
import { FieldsAPI } from '../api/fields';
import { PredicateEditor } from '../components/PredicateEditor';
import { DepositFilterPreview } from '../components/DepositFilterPreview';
import { Breadcrumbs } from '../components/Breadcrumbs';
import {
  DEPOSIT_BUCKET_KEYS, DEPOSIT_BUCKET_LABELS,
  type DepositBucketKey, type DepositFilter, type FieldCatalog, type Predicate,
} from '../types';

//--- All four predicates the editor manages, keyed by canonical bucket name.
type SlotMap = Record<DepositBucketKey, Predicate | null>;

const EMPTY_SLOTS: SlotMap = {
  cash_deposit:    null,
  cash_withdrawal: null,
  promotion:       null,
  rebate:          null,
};

function BucketSection({
  bucketKey, predicate, filterable, onChange,
}: {
  bucketKey:  DepositBucketKey;
  predicate:  Predicate | null;
  filterable: FieldCatalog['filterable_by_source'] extends infer T ? any : never;
  onChange:   (next: Predicate | null) => void;
}) {
  const label = DEPOSIT_BUCKET_LABELS[bucketKey];
  return (
    <div className="card p-5 space-y-3">
      <div className="flex items-center justify-between">
        <div>
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">{label}</div>
          <div className="text-[11px] font-mono text-ink-400">{bucketKey}</div>
        </div>
        {predicate ? (
          <span className="inline-block px-2 py-0.5 text-[10px] font-semibold bg-emerald-600 text-white rounded">
            predicate set
          </span>
        ) : (
          <span className="inline-block px-2 py-0.5 text-[10px] font-semibold bg-ink-100 text-ink-500 rounded">
            empty — field returns 0
          </span>
        )}
      </div>
      <PredicateEditor
        source="deal"
        filterable={filterable}
        predicate={predicate}
        onChange={onChange}
      />
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
  const [slots, setSlots] = useState<SlotMap>(EMPTY_SLOTS);

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
      setSlots({
        cash_deposit:    f.cash_deposit    ?? null,
        cash_withdrawal: f.cash_withdrawal ?? null,
        promotion:       f.promotion       ?? null,
        rebate:          f.rebate          ?? null,
      });
    }).catch(e => setError(e.message ?? 'failed to load'));
  }, [editing, id]);

  const setSlot = (key: DepositBucketKey, p: Predicate | null) =>
    setSlots(prev => ({ ...prev, [key]: p }));

  const onSave = async () => {
    if (!name.trim()) { setError('Name is required.'); return; }
    setBusy(true); setError(null);
    try {
      const payload = {
        name, description,
        cash_deposit:    slots.cash_deposit,
        cash_withdrawal: slots.cash_withdrawal,
        promotion:       slots.promotion,
        rebate:          slots.rebate,
      };
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
          Define one predicate per standard cash-flow category for this broker. The four buckets are fixed
          (<span className="font-mono">cash_deposit</span>, <span className="font-mono">cash_withdrawal</span>,
          <span className="font-mono"> promotion</span>, <span className="font-mono">rebate</span>);
          templates use the matching <span className="font-mono">sum_…</span> / <span className="font-mono">count_…</span>
          fields and the engine picks each predicate from the ready-made's bound DepositFilter at run time.
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

      {DEPOSIT_BUCKET_KEYS.map(key => (
        <BucketSection
          key={key}
          bucketKey={key}
          predicate={slots[key]}
          filterable={dealFilterable}
          onChange={p => setSlot(key, p)}
        />
      ))}

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Preview</div>
        <p className="text-xs text-ink-500">
          Pick a manager + (optional) account filter + date range, then click "Preview matches". Every cash-flow
          deal in the window is tagged with the bucket(s) whose predicate matched.
        </p>
        <DepositFilterPreview slots={slots} />
      </div>

      <div className="flex justify-end gap-2">
        <button type="button" className="btn-secondary" onClick={() => nav('/deposit-filters')} disabled={busy}>
          Cancel
        </button>
        <button type="button" className="btn-primary" onClick={onSave}
                disabled={busy || !name.trim()}>
          {busy ? 'Saving…' : editing ? 'Save changes' : 'Create deposit filter'}
        </button>
      </div>
    </div>
  );
}
