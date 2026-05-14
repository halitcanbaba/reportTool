import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { TemplatesAPI } from '../api/templates';
import type { Template } from '../types';
import { fmtDateTime } from '../utils/format';
import { copyName } from '../lib/duplicate';

export function TemplateListPage() {
  const [items, setItems] = useState<Template[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const reload = async () => {
    setLoading(true);
    try { setItems(await TemplatesAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete template "${name}"? Jobs that reference it will block the delete.`)) return;
    try {
      await TemplatesAPI.remove(id);
      reload();
    } catch (e: any) {
      alert(e.message ?? 'delete failed');
    }
  };

  const onDuplicate = async (t: Template) => {
    try {
      const full = await TemplatesAPI.get(t.id);
      const { id, created_at, updated_at, ...rest } = full;
      await TemplatesAPI.create({ ...rest, name: copyName(t.name, items.map(i => i.name)) });
      reload();
    } catch (e: any) {
      alert(e.message ?? 'duplicate failed');
    }
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Report Templates</h1>
          <p className="text-sm text-ink-500 mt-1">Design once, run with different date ranges and account filters.</p>
        </div>
        <Link to="/templates/new" className="btn-primary">+ New template</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No templates yet.</div>
          <Link to="/templates/new" className="btn-primary">Design the first template</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <div className="card overflow-hidden">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Name</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Date Params</th>
                <th className="px-4 py-3 text-right font-medium text-ink-600 uppercase text-xs tracking-wide">Columns</th>
                <th className="px-4 py-3 text-right font-medium text-ink-600 uppercase text-xs tracking-wide">Top N</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Updated</th>
                <th className="px-4 py-3"></th>
              </tr>
            </thead>
            <tbody>
              {items.map(t => (
                <tr key={t.id} className="border-b border-ink-50 last:border-0">
                  <td className="px-4 py-3">
                    <div className="font-medium">{t.name}</div>
                    {t.description && <div className="text-xs text-ink-500">{t.description}</div>}
                  </td>
                  <td className="px-4 py-3 font-mono text-xs">{t.date_params.join(', ') || <span className="text-ink-400">none</span>}</td>
                  <td className="px-4 py-3 text-right tabular-nums">{t.columns.length}</td>
                  <td className="px-4 py-3 text-right tabular-nums">{t.default_top_n}</td>
                  <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(t.updated_at)}</td>
                  <td className="px-4 py-3 text-right">
                    <Link to={`/templates/${t.id}/run`} className="btn-primary text-xs px-2 py-1 mr-2">Run</Link>
                    <Link to={`/templates/${t.id}/edit`} className="btn-secondary text-xs px-2 py-1 mr-2">Edit</Link>
                    <button onClick={() => onDuplicate(t)} className="btn-secondary text-xs px-2 py-1 mr-2">Duplicate</button>
                    <button onClick={() => onDelete(t.id, t.name)} className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50">Delete</button>
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
