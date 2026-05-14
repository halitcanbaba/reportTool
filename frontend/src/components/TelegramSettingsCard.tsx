import { useEffect, useState } from 'react';
import { SettingsAPI } from '../api/settings';
import type { TelegramSettings } from '../types';

export function TelegramSettingsCard() {
  const [data, setData] = useState<TelegramSettings | null>(null);
  const [loading, setLoading] = useState(true);
  const [editing, setEditing] = useState(false);
  const [newToken, setNewToken] = useState('');
  const [defaultChat, setDefaultChat] = useState('');
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ kind: 'ok' | 'err'; text: string } | null>(null);

  const reload = async () => {
    setLoading(true);
    try {
      const d = await SettingsAPI.telegramGet();
      setData(d);
      setDefaultChat(d.default_chat_id);
    } catch (e: any) {
      setMsg({ kind: 'err', text: e.message ?? 'load failed' });
    } finally { setLoading(false); }
  };
  useEffect(() => { reload(); }, []);

  const save = async () => {
    setBusy(true); setMsg(null);
    try {
      const body: { bot_token?: string; default_chat_id?: string } = { default_chat_id: defaultChat };
      if (newToken.trim()) body.bot_token = newToken.trim();
      await SettingsAPI.telegramPut(body);
      setNewToken('');
      setEditing(false);
      await reload();
      setMsg({ kind: 'ok', text: 'Settings saved.' });
    } catch (e: any) {
      setMsg({ kind: 'err', text: e.message ?? 'save failed' });
    } finally { setBusy(false); }
  };

  const test = async () => {
    setBusy(true); setMsg(null);
    try {
      const r = await SettingsAPI.telegramTest(defaultChat || undefined);
      setMsg({ kind: 'ok', text: `Test sent to chat ${r.chat_id}.` });
    } catch (e: any) {
      setMsg({ kind: 'err', text: e.message ?? 'test failed' });
    } finally { setBusy(false); }
  };

  return (
    <div className="card p-5 space-y-3">
      <div className="flex items-center justify-between">
        <div>
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Telegram bot</div>
          <div className="text-[11px] text-ink-500">
            Used by schedules to deliver CSV reports. Token stored encrypted (AES-256-GCM).
          </div>
        </div>
        {data?.configured
          ? <span className="text-xs px-2 py-1 rounded bg-emerald-50 text-emerald-700 border border-emerald-200">configured</span>
          : <span className="text-xs px-2 py-1 rounded bg-amber-50 text-amber-700 border border-amber-200">not configured</span>}
      </div>

      {loading && <div className="text-xs text-ink-400">Loading…</div>}

      {!loading && data && (
        <>
          <div>
            <label className="label">Bot token</label>
            {!editing ? (
              <div className="flex items-center gap-2">
                <code className="font-mono text-xs bg-ink-50 border border-ink-100 rounded px-2 py-1 flex-1">
                  {data.bot_token_masked || <span className="text-ink-400">none</span>}
                </code>
                <button className="btn-secondary text-xs px-2 py-1" onClick={() => setEditing(true)}>Change</button>
              </div>
            ) : (
              <div className="flex items-center gap-2">
                <input className="input font-mono text-xs flex-1" type="password"
                       value={newToken} onChange={e => setNewToken(e.target.value)}
                       placeholder="123456:ABC-DEF..." />
                <button className="btn-secondary text-xs px-2 py-1" onClick={() => { setEditing(false); setNewToken(''); }}>Cancel</button>
              </div>
            )}
            <div className="text-[11px] text-ink-500 mt-1">Get from @BotFather. Leave blank when changing default chat only.</div>
          </div>

          <div>
            <label className="label">Default chat ID</label>
            <input className="input font-mono text-xs" value={defaultChat}
                   onChange={e => setDefaultChat(e.target.value)}
                   placeholder="-100xxxxxxxxxx or numeric user id" />
            <div className="text-[11px] text-ink-500 mt-1">
              Channels start with <code>-100…</code>. Schedules can override per row.
            </div>
          </div>

          <div className="flex items-center gap-2">
            <button className="btn-primary text-xs px-3 py-1" disabled={busy} onClick={save}>{busy ? 'Working…' : 'Save'}</button>
            <button className="btn-secondary text-xs px-3 py-1" disabled={busy || !data.configured} onClick={test}>Send test message</button>
          </div>

          {msg && (
            <div className={`text-xs rounded px-3 py-2 font-mono ${
              msg.kind === 'ok'
                ? 'bg-emerald-50 text-emerald-800 border border-emerald-200'
                : 'bg-red-50 text-red-800 border border-red-200'
            }`}>{msg.text}</div>
          )}
        </>
      )}
    </div>
  );
}
