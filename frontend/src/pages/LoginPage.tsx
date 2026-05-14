import { useState } from 'react';
import { Navigate, useLocation, useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';

export function LoginPage() {
  const { user, needsSetup, login, loading } = useAuth();
  const nav = useNavigate();
  const location = useLocation();
  const from = (location.state as { from?: string } | null)?.from ?? '/';

  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [busy, setBusy]         = useState(false);
  const [err, setErr]           = useState<string | null>(null);

  if (loading)     return null;
  if (needsSetup)  return <Navigate to="/setup" replace />;
  if (user)        return <Navigate to={from} replace />;

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true); setErr(null);
    try { await login(username.trim(), password); nav(from, { replace: true }); }
    catch (e: any) { setErr(e.message ?? 'login failed'); }
    finally        { setBusy(false); }
  };

  return (
    <div className="h-screen flex items-center justify-center bg-ink-50">
      <form onSubmit={submit} className="card p-6 w-[380px] space-y-4">
        <div>
          <div className="text-xl font-semibold text-ink-900">MT5 ReportTool</div>
          <div className="text-xs text-ink-500 mt-1">Sign in to continue</div>
        </div>

        {err && (
          <div className="text-red-700 bg-red-50 border border-red-200 rounded px-3 py-2 text-sm font-mono">
            {err}
          </div>
        )}

        <div>
          <label className="label">Username</label>
          <input className="input" autoFocus autoComplete="username"
                 value={username} onChange={e => setUsername(e.target.value)} />
        </div>
        <div>
          <label className="label">Password</label>
          <input className="input" type="password" autoComplete="current-password"
                 value={password} onChange={e => setPassword(e.target.value)} />
        </div>

        <button type="submit" className="btn-primary w-full" disabled={busy || !username || !password}>
          {busy ? 'Signing in…' : 'Sign in'}
        </button>
      </form>
    </div>
  );
}
