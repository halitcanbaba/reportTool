import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { UsersAPI } from '../api/users';
import { useAuth } from '../contexts/AuthContext';
import { fmtDateTime } from '../utils/format';
import type { AppUser } from '../types';

function RoleBadge({ role }: { role: AppUser['role'] }) {
  const cls = role === 'admin'
    ? 'bg-blue-50 text-blue-700 border-blue-200'
    : 'bg-ink-50 text-ink-700 border-ink-200';
  return <span className={`text-xs px-2 py-0.5 rounded border ${cls}`}>{role}</span>;
}

export function UserListPage() {
  const { user: me } = useAuth();
  const [items, setItems]   = useState<AppUser[]>([]);
  const [loading, setLoad]  = useState(true);
  const [error, setError]   = useState<string | null>(null);

  const reload = async () => {
    setLoad(true);
    try { setItems(await UsersAPI.list()); setError(null); }
    catch (e: any) { setError(e.message ?? 'failed'); }
    finally        { setLoad(false); }
  };
  useEffect(() => { reload(); }, []);

  const onDelete = async (u: AppUser) => {
    if (!confirm(`Delete user "${u.username}"?`)) return;
    try { await UsersAPI.remove(u.id); reload(); }
    catch (e: any) { alert(e.message ?? 'delete failed'); }
  };

  const onToggleActive = async (u: AppUser) => {
    try { await UsersAPI.patch(u.id, { active: !u.active }); reload(); }
    catch (e: any) { alert(e.message ?? 'update failed'); }
  };

  return (
    <div>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h1 className="text-2xl font-semibold text-ink-900">Users</h1>
          <p className="text-sm text-ink-500 mt-1">Admin: full access. Viewer: read-only.</p>
        </div>
        <Link to="/users/new" className="btn-primary">+ New user</Link>
      </div>

      {error   && <div className="card p-4 mb-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>}
      {loading && <div className="text-ink-400 text-sm">Loading…</div>}

      {!loading && items.length > 0 && (
        <div className="card overflow-hidden">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Username</th>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Role</th>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Active</th>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Last login</th>
                <th className="px-4 py-3 text-left  font-medium text-ink-600 uppercase text-xs tracking-wide">Created</th>
                <th className="px-4 py-3"></th>
              </tr>
            </thead>
            <tbody>
              {items.map(u => (
                <tr key={u.id} className={`border-b border-ink-50 last:border-0 ${u.active ? '' : 'opacity-60'}`}>
                  <td className="px-4 py-3 font-medium">
                    {u.username}
                    {me?.id === u.id && <span className="ml-2 text-[11px] text-ink-500">(you)</span>}
                  </td>
                  <td className="px-4 py-3"><RoleBadge role={u.role} /></td>
                  <td className="px-4 py-3">
                    <label className="inline-flex items-center gap-2 text-xs">
                      <input type="checkbox" checked={u.active}
                             disabled={me?.id === u.id}
                             onChange={() => onToggleActive(u)} />
                      {u.active ? 'active' : 'inactive'}
                    </label>
                  </td>
                  <td className="px-4 py-3 text-xs text-ink-500">{u.last_login_at ? fmtDateTime(u.last_login_at) : <span className="text-ink-400">never</span>}</td>
                  <td className="px-4 py-3 text-xs text-ink-500">{fmtDateTime(u.created_at)}</td>
                  <td className="px-4 py-3 text-right whitespace-nowrap">
                    <Link to={`/users/${u.id}/edit`} className="btn-secondary text-xs px-2 py-1 mr-2">Edit</Link>
                    <button onClick={() => onDelete(u)}
                            disabled={me?.id === u.id}
                            className="btn-secondary text-xs px-2 py-1 text-red-600 hover:bg-red-50 disabled:opacity-40">
                      Delete
                    </button>
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
