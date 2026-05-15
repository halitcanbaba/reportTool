import { useEffect, useMemo, useRef, useState } from 'react';
import { ManagersAPI } from '../api/managers';

type Props = {
  managerId: number | null;
  value: string[];
  onChange: (next: string[]) => void;
};

//--- Split a group path on either `\` or `/`. MT5 typically uses backslash.
function firstSegment(g: string): string {
  const i = g.search(/[\\/]/);
  return i < 0 ? g : g.slice(0, i) || '(root)';
}

//--- Group a flat list of names by their first path segment, preserving order.
function bucketByPrefix(names: string[]): { prefix: string; items: string[] }[] {
  const map = new Map<string, string[]>();
  for (const n of names) {
    const p = firstSegment(n);
    let arr = map.get(p);
    if (!arr) { arr = []; map.set(p, arr); }
    arr.push(n);
  }
  return Array.from(map, ([prefix, items]) => ({ prefix, items }));
}

//--- Does `mask` already cover `group`?  Mask can be an exact name or a
//--- wildcard ending in `*` (the only MT5 wildcard shape we generate here).
function maskCovers(mask: string, group: string): boolean {
  if (mask === group) return true;
  if (mask.endsWith('*')) {
    const prefix = mask.slice(0, -1);
    return group.startsWith(prefix);
  }
  return false;
}

export function GroupPicker({ managerId, value, onChange }: Props) {
  const [groups, setGroups]   = useState<string[]>([]);
  const [loading, setLoading] = useState(false);
  const [loadErr, setLoadErr] = useState<string | null>(null);
  const [loadedFor, setLoadedFor] = useState<number | null>(null);
  const [search, setSearch] = useState('');
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});
  const [open, setOpen] = useState(false);
  const rootRef = useRef<HTMLDivElement>(null);

  //--- Close the dropdown on any mousedown outside the picker.
  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) {
        setOpen(false);
      }
    };
    document.addEventListener('mousedown', onDown);
    return () => document.removeEventListener('mousedown', onDown);
  }, [open]);

  useEffect(() => {
    setGroups([]); setLoadErr(null); setLoadedFor(null); setCollapsed({});
    if (managerId == null) return;
    setLoading(true);
    ManagersAPI.groups(managerId)
      .then(r => { setGroups(r.groups ?? []); setLoadedFor(managerId); })
      .catch(e => setLoadErr(e?.message ?? 'failed to load groups'))
      .finally(() => setLoading(false));
  }, [managerId]);

  //--- Substring filter over the full group list (case-insensitive).
  const matches = useMemo(() => {
    const q = search.trim().toLowerCase();
    if (!q) return groups;
    return groups.filter(g => g.toLowerCase().includes(q));
  }, [groups, search]);

  const buckets = useMemo(() => bucketByPrefix(matches), [matches]);
  const autoCollapse = matches.length > 30 && buckets.length > 1;
  const isCollapsed = (prefix: string) =>
    prefix in collapsed ? collapsed[prefix] : autoCollapse;
  const toggle = (prefix: string) =>
    setCollapsed(prev => ({ ...prev, [prefix]: !isCollapsed(prefix) }));

  const selectedSet = useMemo(() => new Set(value), [value]);
  const coveringMaskFor = (g: string): string | null => {
    for (const m of value) {
      if (m === g) continue;        // exact picks aren't "covered by" themselves
      if (maskCovers(m, g)) return m;
    }
    return null;
  };

  const addMask = (m: string) => {
    if (!m || value.includes(m)) return;
    onChange([...value, m]);
  };
  const removeMask = (m: string) => onChange(value.filter(x => x !== m));

  return (
    <div className="space-y-2 relative" ref={rootRef}>
      {/* Selected chips */}
      <div className="flex flex-wrap items-center gap-1 min-h-[28px]">
        {value.length === 0 ? (
          <span className="text-[11px] text-ink-400 italic">
            No groups picked. Click below to browse, or leave empty to use the manager's defaults.
          </span>
        ) : (
          value.map(m => (
            <span key={m}
                  className="inline-flex items-center gap-1 px-2 py-0.5 rounded border border-emerald-300 bg-emerald-50 text-emerald-900 font-mono text-xs">
              {m}
              <button type="button" className="text-red-500 hover:text-red-700 ml-0.5"
                      title="remove" onClick={() => removeMask(m)}>×</button>
            </span>
          ))
        )}
      </div>

      {/* Search input — opens the dropdown on focus/click */}
      <div className="relative">
        <input
          className="input font-mono text-xs pr-7"
          value={search}
          onChange={e => { setSearch(e.target.value); if (!open) setOpen(true); }}
          onFocus={() => setOpen(true)}
          onClick={() => setOpen(true)}
          placeholder={open ? 'Type to filter groups…' : 'Click to browse groups…'}
        />
        <button type="button"
                onClick={() => setOpen(o => !o)}
                className="absolute right-1 top-1/2 -translate-y-1/2 text-ink-400 hover:text-ink-700 text-xs px-1"
                title={open ? 'Close' : 'Open'}>
          {open ? '▴' : '▾'}
        </button>
      </div>

      {managerId == null && (
        <div className="text-[11px] text-ink-400 italic">
          Select a manager above to browse groups.
        </div>
      )}

      {managerId != null && loading && (
        <div className="text-[11px] text-ink-400">Loading groups from MT5…</div>
      )}

      {managerId != null && loadErr && (
        <div className="text-[11px] text-amber-700">
          ⚠ Could not load groups: <span className="font-mono">{loadErr}</span>
        </div>
      )}

      {/* Dropdown panel — only when open */}
      {open && managerId != null && !loading && !loadErr && loadedFor === managerId && (
        <div className="border border-ink-100 rounded bg-white shadow-sm">
          <div className="px-2 py-1 text-[11px] text-ink-500 bg-ink-50 border-b border-ink-100">
            Matches <span className="font-semibold">{matches.length}</span> of {groups.length} groups
          </div>

          <div className="max-h-72 overflow-auto">
            {matches.length === 0 ? (
              <div className="text-[11px] text-ink-400 p-2 italic">
                No groups match this search.
              </div>
            ) : (
              buckets.map(b => {
                const folded = isCollapsed(b.prefix);
                const folderMask = `${b.prefix}\\*`;
                const folderAdded = selectedSet.has(folderMask);
                return (
                  <div key={b.prefix} className="border-b border-ink-50 last:border-0">
                    <div className="w-full flex items-center justify-between bg-ink-50/60 hover:bg-ink-50">
                      <button type="button"
                              onClick={() => toggle(b.prefix)}
                              className="flex-1 text-left px-2 py-1 text-[11px] text-ink-700 font-mono">
                        <span className="text-ink-400 mr-1">{folded ? '▸' : '▾'}</span>
                        {b.prefix}
                        <span className="text-ink-500 ml-2">({b.items.length})</span>
                      </button>
                      <button type="button"
                              disabled={folderAdded}
                              onClick={() => addMask(folderMask)}
                              title={folderAdded ? 'already added' : `add wildcard ${folderMask}`}
                              className={
                                'px-2 py-1 text-xs ' +
                                (folderAdded
                                  ? 'text-ink-300 cursor-not-allowed'
                                  : 'text-emerald-700 hover:bg-emerald-50')
                              }>
                        {folderAdded ? '✓' : '+ all'}
                      </button>
                    </div>
                    {!folded && (
                      <div>
                        {b.items.map(g => {
                          const exact = selectedSet.has(g);
                          const covered = !exact ? coveringMaskFor(g) : null;
                          const disabled = exact || !!covered;
                          return (
                            <div key={g}
                                 className="flex items-center justify-between hover:bg-ink-50">
                              <span className="text-xs font-mono px-2 py-0.5 pl-5 text-ink-800 truncate">
                                {g}
                                {exact && <span className="ml-2 text-[10px] text-ink-400">(added)</span>}
                                {covered && <span className="ml-2 text-[10px] text-ink-400">(covered by {covered})</span>}
                              </span>
                              <button type="button"
                                      disabled={disabled}
                                      onClick={() => addMask(g)}
                                      title={disabled ? 'already covered' : `add ${g}`}
                                      className={
                                        'px-2 py-0.5 text-xs ' +
                                        (disabled
                                          ? 'text-ink-300 cursor-not-allowed'
                                          : 'text-emerald-700 hover:bg-emerald-50')
                                      }>
                                {disabled ? '✓' : '+'}
                              </button>
                            </div>
                          );
                        })}
                      </div>
                    )}
                  </div>
                );
              })
            )}
          </div>
        </div>
      )}
    </div>
  );
}
