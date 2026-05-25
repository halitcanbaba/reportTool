import { useEffect, useRef, useState } from 'react';

//--- Generic single-select combobox with type-to-filter. Used wherever a
//--- plain <select> grows past ~10 entries and becomes hard to scan —
//--- ReadyMadeEditPage's template / account-filter pickers being the
//--- motivating case. Each option exposes:
//---   value:   the underlying id passed back via onChange
//---   label:   the primary display string (used for search matching too)
//---   hint?:   right-aligned secondary text (e.g. folder name, bucket count)
//---   sub?:    second line under the label (optional, for richer entries)

export type SearchableOption<V extends string | number> = {
  value: V;
  label: string;
  hint?: string;
  sub?:  string;
};

type Props<V extends string | number> = {
  options:     SearchableOption<V>[];
  value:       V | null;
  onChange:    (v: V | null) => void;
  placeholder?: string;
  emptyLabel?:  string;     // shown when value=null
  className?:   string;
};

export function SearchableSelect<V extends string | number>({
  options, value, onChange, placeholder = 'search…', emptyLabel = '— select —', className = '',
}: Props<V>) {
  const [open, setOpen] = useState(false);
  const [q, setQ] = useState('');
  const wrapRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  //--- Close on outside click. Pointerdown captures the click before any
  //--- option's onClick fires so selecting still works.
  useEffect(() => {
    if (!open) return;
    const onDown = (e: PointerEvent) => {
      if (wrapRef.current && !wrapRef.current.contains(e.target as Node)) {
        setOpen(false);
        setQ('');
      }
    };
    document.addEventListener('pointerdown', onDown);
    return () => document.removeEventListener('pointerdown', onDown);
  }, [open]);

  //--- Focus the search input the moment the popover opens — saves a tap
  //--- and lets the user just start typing.
  useEffect(() => {
    if (open) setTimeout(() => inputRef.current?.focus(), 0);
  }, [open]);

  const selected = options.find(o => o.value === value) ?? null;

  const filtered = q.trim() === ''
    ? options
    : options.filter(o => {
        const needle = q.toLowerCase();
        return o.label.toLowerCase().includes(needle)
            || (o.hint ?? '').toLowerCase().includes(needle)
            || (o.sub  ?? '').toLowerCase().includes(needle);
      });

  return (
    <div className={`relative ${className}`} ref={wrapRef}>
      <button
        type="button"
        onClick={() => setOpen(o => !o)}
        className="input text-left flex items-center justify-between gap-2 w-full"
      >
        <span className={selected ? 'text-ink-900' : 'text-ink-400'}>
          {selected ? (
            <span>
              {selected.label}
              {selected.hint && (
                <span className="ml-2 text-xs text-ink-500">{selected.hint}</span>
              )}
            </span>
          ) : emptyLabel}
        </span>
        <span className="text-ink-400 text-xs">▾</span>
      </button>

      {open && (
        <div className="absolute z-30 left-0 right-0 mt-1 bg-white border border-ink-200 rounded-md shadow-lg max-h-80 overflow-hidden flex flex-col">
          <div className="p-2 border-b border-ink-100 sticky top-0 bg-white">
            <input
              ref={inputRef}
              className="input text-sm w-full"
              placeholder={placeholder}
              value={q}
              onChange={e => setQ(e.target.value)}
              onKeyDown={e => {
                if (e.key === 'Escape') { setOpen(false); setQ(''); }
                if (e.key === 'Enter' && filtered.length > 0) {
                  onChange(filtered[0].value);
                  setOpen(false); setQ('');
                }
              }}
            />
            <div className="text-[10px] text-ink-400 mt-1">
              {filtered.length} / {options.length} · Enter selects first match
            </div>
          </div>
          <ul className="overflow-y-auto">
            {filtered.length === 0 && (
              <li className="px-3 py-2 text-sm text-ink-400">no matches</li>
            )}
            {filtered.map(o => {
              const active = value === o.value;
              return (
                <li key={String(o.value)}>
                  <button
                    type="button"
                    onClick={() => { onChange(o.value); setOpen(false); setQ(''); }}
                    className={
                      'w-full text-left px-3 py-1.5 flex items-start justify-between gap-2 ' +
                      (active ? 'bg-blue-50' : 'hover:bg-ink-50')
                    }
                  >
                    <span className="flex flex-col min-w-0 flex-1">
                      <span className="text-sm truncate">{o.label}</span>
                      {o.sub && <span className="text-[11px] text-ink-500 truncate">{o.sub}</span>}
                    </span>
                    {o.hint && (
                      <span className="text-xs text-ink-500 shrink-0 mt-0.5 font-mono">{o.hint}</span>
                    )}
                  </button>
                </li>
              );
            })}
          </ul>
          {value !== null && (
            <button
              type="button"
              onClick={() => { onChange(null); setOpen(false); setQ(''); }}
              className="px-3 py-1.5 text-xs text-ink-500 border-t border-ink-100 text-left hover:bg-ink-50"
            >
              ✕ Clear selection
            </button>
          )}
        </div>
      )}
    </div>
  );
}
