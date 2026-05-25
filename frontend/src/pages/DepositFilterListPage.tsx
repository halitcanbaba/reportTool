import { useEffect, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { DepositFiltersAPI } from '../api/depositFilters';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import { IconButton, IconFilter, IconEdit, IconDuplicate, IconDelete } from '../components/icons';
import {
  DEPOSIT_BUCKET_KEYS, DEPOSIT_BUCKET_LABELS,
  type DepositBucketKey, type DepositFilter,
} from '../types';

function BucketDot({ on, label }: { on: boolean; label: string }) {
  return (
    <span
      className={
        'inline-flex items-center justify-center w-5 h-5 rounded text-[10px] font-semibold ' +
        (on
          ? 'bg-emerald-500 text-white'
          : 'bg-ink-100 text-ink-400 border border-ink-200')
      }
      title={`${label} — ${on ? 'set' : 'empty'}`}
    >
      {on ? '✓' : '—'}
    </span>
  );
}

export function DepositFilterListPage() {
  const nav = useNavigate();
  const [items, setItems] = useState<DepositFilter[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await DepositFiltersAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete deposit filter "${name}"?`)) return;
    await DepositFiltersAPI.remove(id);
    reload();
  };

  const onDuplicate = async (f: DepositFilter) => {
    try {
      const full = await DepositFiltersAPI.get(f.id);
      const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _c; void _u;
      await DepositFiltersAPI.create({ ...rest, name: copyName(f.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  const isSet = (f: DepositFilter, k: DepositBucketKey) => !!f[k];

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Deposit Filters</h1>
          <p className="text-sm text-ink-500 mt-1">
            Per-broker cash-flow presets. Four fixed buckets — bind a ready-made to a preset so
            <span className="font-mono"> sum_cash_deposit</span> /
            <span className="font-mono"> count_promotion</span> etc. resolve with that broker's rules.
          </p>
        </div>
        <Link to="/deposit-filters/new" className="btn-primary">+ New deposit filter</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No deposit filters yet.</div>
          <Link to="/deposit-filters/new" className="btn-primary">Add the first preset</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <div className="card overflow-hidden">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Name</th>
                {DEPOSIT_BUCKET_KEYS.map(k => (
                  <th key={k} className="px-2 py-3 text-center font-medium text-ink-600 uppercase text-[10px] tracking-wide"
                      title={DEPOSIT_BUCKET_LABELS[k]}>
                    {k}
                  </th>
                ))}
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Description</th>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Updated</th>
                <th className="px-4 py-3"></th>
              </tr>
            </thead>
            <tbody>
              {items.map(f => (
                <tr key={f.id} className="border-b border-ink-50 last:border-0">
                  <td className="px-4 py-3">
                    <span className="inline-flex items-center gap-2">
                      <IconFilter className="text-ink-500 shrink-0" />
                      <span className="font-medium">{f.name}</span>
                    </span>
                  </td>
                  {DEPOSIT_BUCKET_KEYS.map(k => (
                    <td key={k} className="px-2 py-3 text-center">
                      <BucketDot on={isSet(f, k)} label={DEPOSIT_BUCKET_LABELS[k]} />
                    </td>
                  ))}
                  <td className="px-4 py-3 text-ink-600">{f.description || <span className="text-ink-400">—</span>}</td>
                  <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(f.updated_at)}</td>
                  <td className="px-4 py-3 text-right whitespace-nowrap">
                    <IconButton title="Edit"      onClick={() => nav(`/deposit-filters/${f.id}/edit`)}><IconEdit /></IconButton>
                    <IconButton title="Duplicate" onClick={() => onDuplicate(f)}><IconDuplicate /></IconButton>
                    <IconButton title="Delete"    danger onClick={() => onDelete(f.id, f.name)}><IconDelete /></IconButton>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
