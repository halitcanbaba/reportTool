import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { BlueprintsAPI } from '../api/blueprints';
import { astToText } from '../lib/exprChips';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';
import type { FormulaBlueprint } from '../types';

export function BlueprintListPage() {
  const [items, setItems] = useState<FormulaBlueprint[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await BlueprintsAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete blueprint "${name}"?`)) return;
    await BlueprintsAPI.remove(id);
    reload();
  };

  const onDuplicate = async (b: FormulaBlueprint) => {
    try {
      const full = await BlueprintsAPI.get(b.id);
      const { id, created_at, updated_at, ...rest } = full;
      await BlueprintsAPI.create({ ...rest, name: copyName(b.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Formula Blueprints</h1>
          <p className="text-sm text-ink-500 mt-1">Reusable formula building blocks for fast template design.</p>
        </div>
        <Link to="/blueprints/new" className="btn-primary">+ New blueprint</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No blueprints yet.</div>
          <Link to="/blueprints/new" className="btn-primary">Save your first blueprint</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <div className="card overflow-hidden">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Name</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Date params</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Formula</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Updated</th>
                <th className="px-4 py-3"></th>
              </tr>
            </thead>
            <tbody>
              {items.map(b => {
                let preview = '';
                try { preview = b.expr ? astToText(b.expr) : ''; } catch { preview = '(unrenderable)'; }
                return (
                  <tr key={b.id} className="border-b border-ink-50 last:border-0">
                    <td className="px-4 py-3">
                      <div className="font-medium">{b.name}</div>
                      {b.description && <div className="text-xs text-ink-500">{b.description}</div>}
                    </td>
                    <td className="px-4 py-3 font-mono text-xs">{b.date_params.join(', ') || <span className="text-ink-400">—</span>}</td>
                    <td className="px-4 py-3 font-mono text-xs text-ink-700" style={{ maxWidth: 400 }}>
                      <div className="truncate" title={preview}>{preview}</div>
                    </td>
                    <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(b.updated_at)}</td>
                    <td className="px-4 py-3 text-right">
                      <Link to={`/blueprints/${b.id}/edit`} className="btn-secondary text-xs px-2 py-1 mr-2">Edit</Link>
                      <button onClick={() => onDuplicate(b)} className="btn-secondary text-xs px-2 py-1 mr-2">Duplicate</button>
                      <button onClick={() => onDelete(b.id, b.name)} className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50">Delete</button>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
