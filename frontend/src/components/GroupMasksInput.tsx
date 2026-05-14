import { useCallback, useEffect, useMemo, useState } from 'react';
import { ManagersAPI } from '../api/managers';

type Props = {
  managerId: number | null;
  value: string[];
  onChange: (next: string[]) => void;
};

export function GroupMasksInput({ managerId, value, onChange }: Props) {
  const [discovered, setDiscovered] = useState<string[]>([]);
  const [loading, setLoading] = useState(false);
  const [loadErr, setLoadErr] = useState<string | null>(null);
  const [loadedFor, setLoadedFor] = useState<number | null>(null);   // managerId we cached for
  const [open, setOpen] = useState(false);
  const [filter, setFilter] = useState('');
  const [custom, setCustom] = useState('');

  //--- Reset cache when manager changes; do NOT auto-fetch (502 risk).
  useEffect(() => {
    setDiscovered([]);
    setLoadErr(null);
    setLoadedFor(null);
  }, [managerId]);

  const loadGroups = useCallback(async (force = false) => {
    if (managerId == null) return;
    if (!force && loadedFor === managerId) return;     // already loaded
    setLoading(true); setLoadErr(null);
    try {
      const r = await ManagersAPI.groups(managerId);
      setDiscovered(r.groups ?? []);
      setLoadedFor(managerId);
    } catch (e: any) {
      setLoadErr(e.message ?? 'failed to load groups');
    } finally {
      setLoading(false);
    }
  }, [managerId, loadedFor]);

  //--- Open the picker; lazy-load groups on first open.
  const togglePicker = () => {
    const willOpen = !open;
    setOpen(willOpen);
    if (willOpen && managerId != null && loadedFor !== managerId) void loadGroups();
  };

  const selected = useMemo(() => new Set(value), [value]);
  const filteredOptions = useMemo(() => {
    const q = filter.trim().toLowerCase();
    return discovered.filter(g => !q || g.toLowerCase().includes(q));
  }, [discovered, filter]);

  const addMask = (m: string) => {
    const trimmed = m.trim();
    if (!trimmed) return;
    if (value.includes(trimmed)) return;
    onChange([...value, trimmed]);
  };
  const removeMask = (m: string) => onChange(value.filter(x => x !== m));
  const submitCustom = () => {
    if (!custom.trim()) return;
    addMask(custom);
    setCustom('');
  };

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap items-center gap-1">
        {value.map(m => (
          <span key={m}
                className="inline-flex items-center gap-1 px-2 py-0.5 rounded border border-emerald-300 bg-emerald-50 text-emerald-900 font-mono text-xs">
            {m}
            <button type="button" className="text-red-500 hover:text-red-700 ml-0.5"
                    title="remove" onClick={() => removeMask(m)}>×</button>
          </span>
        ))}
        {value.length === 0 && (
          <span className="text-xs text-ink-400 italic">No group masks. Click "+ Add" to browse, or type a wildcard below.</span>
        )}
        <span className="relative inline-flex">
          <button type="button"
                  className="btn-secondary text-xs px-2 py-0.5"
                  onClick={togglePicker}>
            {open ? 'Close' : '+ Add…'}
          </button>
          {open && (
            <div className="absolute z-30 top-7 left-0 w-80 bg-white border border-ink-200 rounded shadow-lg p-2 space-y-2">
              <div>
                <input className="input text-xs" autoFocus placeholder="search discovered groups…"
                       value={filter} onChange={e => setFilter(e.target.value)} />
              </div>
              <div className="max-h-56 overflow-auto border border-ink-100 rounded">
                {managerId == null && (
                  <div className="text-xs text-ink-400 p-2 italic">Select a manager above to discover groups, or type a custom mask below.</div>
                )}
                {managerId != null && loading && (
                  <div className="text-xs text-ink-400 p-2">Connecting to MT5…</div>
                )}
                {managerId != null && loadErr && (
                  <div className="text-xs p-2 space-y-1">
                    <div className="text-amber-700">⚠ Could not reach manager: <span className="font-mono">{loadErr}</span></div>
                    <div className="text-ink-500">You can still add masks manually below.</div>
                    <button type="button" className="btn-secondary text-[11px] px-2 py-0.5"
                            onClick={() => loadGroups(true)}>Retry</button>
                  </div>
                )}
                {managerId != null && !loading && !loadErr && loadedFor === managerId
                  && filteredOptions.length === 0 && (
                  <div className="text-xs text-ink-400 p-2 italic">No groups discovered.</div>
                )}
                {filteredOptions.map(g => {
                  const isSelected = selected.has(g);
                  return (
                    <button key={g} type="button"
                            disabled={isSelected}
                            onClick={() => addMask(g)}
                            className={
                              'block w-full text-left text-xs font-mono px-2 py-1 ' +
                              (isSelected
                                ? 'text-ink-400 bg-ink-50 cursor-not-allowed'
                                : 'hover:bg-ink-50 text-ink-800')
                            }>
                      {g} {isSelected && <span className="text-[10px]">(added)</span>}
                    </button>
                  );
                })}
              </div>
              <div className="border-t border-ink-100 pt-2">
                <label className="label">Custom mask (wildcard supported)</label>
                <div className="flex items-center gap-1">
                  <input className="input text-xs font-mono flex-1" value={custom}
                         onChange={e => setCustom(e.target.value)}
                         onKeyDown={e => { if (e.key === 'Enter') { e.preventDefault(); submitCustom(); } }}
                         placeholder={'real\\Indonesia\\*'} />
                  <button type="button" className="btn-secondary text-xs px-2 py-1"
                          onClick={submitCustom} disabled={!custom.trim()}>Add</button>
                </div>
                <div className="text-[10px] text-ink-400 mt-1">MT5 wildcards: <code>*</code> = any, paths use <code>\</code>. Works without manager connection.</div>
              </div>
            </div>
          )}
        </span>
      </div>
      {managerId == null && (
        <div className="text-[11px] text-ink-500">Pick a manager above to enable live group discovery and preview (optional — you can still type wildcard masks manually).</div>
      )}
    </div>
  );
}
