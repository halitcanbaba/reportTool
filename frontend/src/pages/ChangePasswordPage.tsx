import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { AuthAPI } from '../api/auth';
import { useAuth } from '../contexts/AuthContext';
import { Breadcrumbs } from '../components/Breadcrumbs';

export function ChangePasswordPage() {
  const { user } = useAuth();
  const nav = useNavigate();
  const [oldPw, setOldPw]   = useState('');
  const [newPw, setNewPw]   = useState('');
  const [confirm, setConfirm] = useState('');
  const [busy, setBusy]     = useState(false);
  const [msg, setMsg]       = useState<{ kind: 'ok'|'err'; text: string } | null>(null);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (newPw !== confirm) { setMsg({ kind: 'err', text: 'new passwords do not match' }); return; }
    if (newPw.length < 6)  { setMsg({ kind: 'err', text: 'password must be at least 6 characters' }); return; }
    setBusy(true); setMsg(null);
    try {
      await AuthAPI.changePassword(oldPw, newPw);
      setMsg({ kind: 'ok', text: 'Password changed. Other sessions have been signed out.' });
      setOldPw(''); setNewPw(''); setConfirm('');
    } catch (e: any) {
      setMsg({ kind: 'err', text: e.message ?? 'change failed' });
    } finally { setBusy(false); }
  };

  return (
    <div className="max-w-md space-y-4">
      <div>
        <Breadcrumbs items={[{ label: 'Account' }, { label: 'Change password' }]} />
        <h1 className="text-2xl font-semibold text-ink-900">Change password</h1>
        <p className="text-sm text-ink-500 mt-1">Signed in as <span className="font-mono">{user?.username}</span> ({user?.role}).</p>
      </div>

      <form onSubmit={submit} className="card p-5 space-y-4">
        {msg && (
          <div className={`rounded px-3 py-2 text-sm font-mono ${
            msg.kind === 'ok'
              ? 'bg-emerald-50 text-emerald-800 border border-emerald-200'
              : 'bg-red-50 text-red-800 border border-red-200'
          }`}>{msg.text}</div>
        )}
        <div>
          <label className="label">Current password</label>
          <input className="input" type="password" autoComplete="current-password"
                 value={oldPw} onChange={e => setOldPw(e.target.value)} />
        </div>
        <div>
          <label className="label">New password</label>
          <input className="input" type="password" autoComplete="new-password"
                 value={newPw} onChange={e => setNewPw(e.target.value)} />
        </div>
        <div>
          <label className="label">Confirm new password</label>
          <input className="input" type="password" autoComplete="new-password"
                 value={confirm} onChange={e => setConfirm(e.target.value)} />
        </div>
        <div className="flex justify-end gap-2">
          <button type="button" className="btn-secondary" onClick={() => nav(-1)}>Back</button>
          <button type="submit" className="btn-primary" disabled={busy || !oldPw || !newPw || !confirm}>
            {busy ? 'Saving…' : 'Update password'}
          </button>
        </div>
      </form>
    </div>
  );
}
