import { useEffect, useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { DealFiltersAPI } from '../api/dealFilters';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import { IconButton, IconFilter, IconEdit, IconDuplicate, IconDelete } from '../components/icons';
import type { DealFilter } from '../types';

export function DealFilterListPage() {
  const nav = useNavigate();
  const [items, setItems] = useState<DealFilter[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await DealFiltersAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete deal filter "${name}"?`)) return;
    await DealFiltersAPI.remove(id);
    reload();
  };

  const onDuplicate = async (f: DealFilter) => {
    try {
      const full = await DealFiltersAPI.get(f.id);
      const { id: _id, created_at: _c, updated_at: _u, ...rest } = full;
      void _id; void _c; void _u;
      await DealFiltersAPI.create({ ...rest, name: copyName(f.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Deal Filters</h1>
          <p className="text-sm text-ink-500 mt-1">
            Saved cash-flow classifiers (cash deposits, promo bonuses, etc.) — pluggable into any formula aggregator's predicate.
          </p>
        </div>
        <Link to="/deal-filters/new" className="btn-primary">+ New deal filter</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No deal filters yet.</div>
          <Link to="/deal-filters/new" className="btn-primary">Add the first filter</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <div className="card overflow-hidden">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Name</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Description</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Updated</th>
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
                  <td className="px-4 py-3 text-ink-600">{f.description || <span className="text-ink-400">—</span>}</td>
                  <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(f.updated_at)}</td>
                  <td className="px-4 py-3 text-right whitespace-nowrap">
                    <IconButton title="Edit"      onClick={() => nav(`/deal-filters/${f.id}/edit`)}><IconEdit /></IconButton>
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
