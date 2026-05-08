import { Link } from 'react-router-dom';
import { useManagers } from '../hooks/useManagers';
import { ManagersAPI } from '../api/managers';
import { fmtDateTime } from '../utils/format';
import { useState } from 'react';

export function ManagerListPage() {
  const { items, loading, error, reload } = useManagers();
  const [testing, setTesting] = useState<number | null>(null);
  const [results, setResults] = useState<Record<number, string>>({});

  const onTest = async (id: number) => {
    setTesting(id);
    try {
      const r = await ManagersAPI.test(id);
      setResults(s => ({ ...s, [id]: r.connected ? `✓ ${r.users_sample ?? 0} users sampled` : `× ${r.error ?? 'failed'}` }));
    } catch (e: any) {
      setResults(s => ({ ...s, [id]: `× ${e.message}` }));
    } finally {
      setTesting(null);
    }
  };

  const onDelete = async (id: number, name: string) => {
    if (!confirm(`Delete manager "${name}"? This cannot be undone.`)) return;
    await ManagersAPI.remove(id);
    reload();
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Managers</h1>
          <p className="text-sm text-ink-500 mt-1">MT5 manager accounts and their report filters.</p>
        </div>
        <Link to="/managers/new" className="btn-primary">+ New manager</Link>
      </div>

      {error && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading ...</div>}

      {!loading && items.length === 0 && (
        <div className="card p-12 text-center">
          <div className="text-ink-400 mb-4">No managers configured yet.</div>
          <Link to="/managers/new" className="btn-primary">Add the first manager</Link>
        </div>
      )}

      {!loading && items.length > 0 && (
        <div className="card overflow-hidden">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Name</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Brand</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Region</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Server</th>
                <th className="px-4 py-3 text-right font-medium text-ink-600 uppercase text-xs tracking-wide">Login</th>
                <th className="px-4 py-3 text-left font-medium text-ink-600 uppercase text-xs tracking-wide">Updated</th>
                <th className="px-4 py-3"></th>
              </tr>
            </thead>
            <tbody>
              {items.map(m => (
                <tr key={m.id} className="border-b border-ink-50 last:border-0">
                  <td className="px-4 py-3 font-medium">{m.name}</td>
                  <td className="px-4 py-3">{m.brand}</td>
                  <td className="px-4 py-3">{m.region}</td>
                  <td className="px-4 py-3 font-mono text-xs">{m.server}</td>
                  <td className="px-4 py-3 text-right tabular-nums font-mono">{m.manager_login}</td>
                  <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(m.updated_at)}</td>
                  <td className="px-4 py-3">
                    <div className="flex items-center gap-2 justify-end">
                      {results[m.id] && <span className="text-xs text-ink-600">{results[m.id]}</span>}
                      <button onClick={() => onTest(m.id)} disabled={testing === m.id}
                              className="btn-secondary text-xs px-2 py-1">
                        {testing === m.id ? 'Testing…' : 'Test'}
                      </button>
                      <Link to={`/managers/${m.id}/edit`} className="btn-secondary text-xs px-2 py-1">Edit</Link>
                      <button onClick={() => onDelete(m.id, m.name)} className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50">Delete</button>
                    </div>
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
