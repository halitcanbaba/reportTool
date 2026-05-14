import { useState } from 'react';
import { Navigate, useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';

export function SetupPage() {
  const { needsSetup, user, setupAdmin, loading } = useAuth();
  const nav = useNavigate();

  const [username, setUsername] = useState('admin');
  const [password, setPassword] = useState('');
  const [confirm,  setConfirm]  = useState('');
  const [busy, setBusy]         = useState(false);
  const [err, setErr]           = useState<string | null>(null);

  if (loading) return null;
  //--- If setup is already done OR a user is logged in, this page is over.
  if (!needsSetup) return <Navigate to={user ? '/' : '/login'} replace />;

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (password !== confirm) { setErr('passwords do not match'); return; }
    if (password.length < 6)  { setErr('password must be at least 6 characters'); return; }
    setBusy(true); setErr(null);
    try { await setupAdmin(username.trim(), password); nav('/', { replace: true }); }
    catch (e: any) { setErr(e.message ?? 'setup failed'); }
    finally        { setBusy(false); }
  };

  return (
    <div className="h-screen flex items-center justify-center bg-ink-50">
      <form onSubmit={submit} className="card p-6 w-[420px] space-y-4">
        <div>
          <div className="text-xl font-semibold text-ink-900">First-run setup</div>
          <div className="text-xs text-ink-500 mt-1">
            Create the initial administrator account. After this, additional users
            can be added from the Users page.
          </div>
        </div>

        {err && (
          <div className="text-red-700 bg-red-50 border border-red-200 rounded px-3 py-2 text-sm font-mono">
            {err}
          </div>
        )}

        <div>
          <label className="label">Admin username</label>
          <input className="input" autoFocus autoComplete="username"
                 value={username} onChange={e => setUsername(e.target.value)} />
          <div className="text-[11px] text-ink-500 mt-1">Letters, digits, _ . - allowed.</div>
        </div>
        <div>
          <label className="label">Password</label>
          <input className="input" type="password" autoComplete="new-password"
                 value={password} onChange={e => setPassword(e.target.value)} />
          <div className="text-[11px] text-ink-500 mt-1">Minimum 6 characters.</div>
        </div>
        <div>
          <label className="label">Confirm password</label>
          <input className="input" type="password" autoComplete="new-password"
                 value={confirm} onChange={e => setConfirm(e.target.value)} />
        </div>

        <button type="submit" className="btn-primary w-full"
                disabled={busy || !username || !password || !confirm}>
          {busy ? 'Creating…' : 'Create admin & sign in'}
        </button>
      </form>
    </div>
  );
}
