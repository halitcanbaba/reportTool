import { useEffect, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { UsersAPI } from '../api/users';
import { Breadcrumbs } from '../components/Breadcrumbs';
import type { UserRole } from '../types';

export function UserEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [username, setUsername]   = useState('');
  const [role, setRole]           = useState<UserRole>('viewer');
  const [active, setActive]       = useState(true);
  const [password, setPassword]   = useState('');           // create: required; edit: optional reset
  const [confirm, setConfirm]     = useState('');
  const [busy, setBusy]           = useState(false);
  const [err, setErr]             = useState<string | null>(null);

  useEffect(() => {
    if (!editing) return;
    UsersAPI.get(Number(id)).then(u => {
      setUsername(u.username);
      setRole(u.role);
      setActive(u.active);
    });
  }, [editing, id]);

  const save = async () => {
    if (!editing) {
      if (!username.trim()) { setErr('username required'); return; }
      if (password.length < 6) { setErr('password must be at least 6 characters'); return; }
      if (password !== confirm) { setErr('passwords do not match'); return; }
    } else if (password) {
      if (password.length < 6) { setErr('password must be at least 6 characters'); return; }
      if (password !== confirm) { setErr('passwords do not match'); return; }
    }

    setBusy(true); setErr(null);
    try {
      if (!editing) {
        await UsersAPI.create({ username: username.trim(), password, role, active });
      } else {
        await UsersAPI.patch(Number(id), { role, active });
        if (password) await UsersAPI.resetPassword(Number(id), password);
      }
      nav('/users');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <div className="space-y-6 max-w-xl">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'Users', to: '/users' },
            { label: editing ? 'Edit user' : 'New user' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit user' : 'New user'}</h1>
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/users')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        </div>
      </div>

      {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="card p-5 space-y-4">
        <div>
          <label className="label">Username</label>
          <input className="input" value={username}
                 disabled={editing}
                 onChange={e => setUsername(e.target.value)}
                 placeholder="ops_analyst" />
          {editing && <div className="text-[11px] text-ink-500 mt-1">Username can't be changed.</div>}
        </div>
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Role</label>
            <select className="input" value={role} onChange={e => setRole(e.target.value as UserRole)}>
              <option value="admin">admin</option>
              <option value="viewer">viewer</option>
            </select>
            <div className="text-[11px] text-ink-500 mt-1">
              admin: full access · viewer: read-only.
            </div>
          </div>
          <div>
            <label className="label">Status</label>
            <label className="flex items-center gap-2 mt-2 text-sm">
              <input type="checkbox" checked={active} onChange={e => setActive(e.target.checked)} />
              Active
            </label>
            <div className="text-[11px] text-ink-500 mt-1">Deactivating drops all sessions immediately.</div>
          </div>
        </div>
      </div>

      <div className="card p-5 space-y-4">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">
          {editing ? 'Reset password (optional)' : 'Password'}
        </div>
        <div>
          <label className="label">{editing ? 'New password' : 'Password'}</label>
          <input className="input" type="password" autoComplete="new-password"
                 value={password} onChange={e => setPassword(e.target.value)}
                 placeholder={editing ? 'leave blank to keep current password' : 'min 6 characters'} />
        </div>
        <div>
          <label className="label">Confirm</label>
          <input className="input" type="password" autoComplete="new-password"
                 value={confirm} onChange={e => setConfirm(e.target.value)} />
        </div>
        {editing && password && (
          <div className="text-[11px] text-amber-700 bg-amber-50 border border-amber-200 rounded px-3 py-2">
            Saving will sign this user out of all their existing sessions.
          </div>
        )}
      </div>
    </div>
  );
}
