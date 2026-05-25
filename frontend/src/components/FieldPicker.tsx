import { useMemo, useState } from 'react';
import { useDraggable } from '@dnd-kit/core';
import type { FieldCatalog, FieldDef } from '../types';

type WrapperProps = {
  catalog: FieldCatalog;
  filter?: (f: FieldDef) => boolean;     // restrict (e.g. numeric only)
  onPick:   (f: FieldDef) => void;        // click-to-pick (fallback / fast path)
  placeholder?: string;
  defaultOpen?: boolean;                  // start with the dropdown already open
};

//--- One draggable row inside the picker.
function DraggableField({ f, onPick }: { f: FieldDef; onPick: (f: FieldDef) => void }) {
  //--- Per-bucket virtual entries share f.name with the base field (e.g.
  //--- "sum_deposit_amount" expanded to one entry per bucket key). Append
  //--- the bucket to the dnd-kit id so each entry is uniquely draggable.
  const dndId = f.default_bucket
    ? `field:${f.name}:${f.default_bucket}`
    : `field:${f.name}`;
  const { attributes, listeners, setNodeRef, isDragging } = useDraggable({
    id: dndId,
    data: { kind: 'field', field: f },
  });
  return (
    <button type="button"
            ref={setNodeRef}
            {...attributes} {...listeners}
            onClick={() => onPick(f)}
            className={
              'w-full text-left px-3 py-1.5 hover:bg-ink-50 flex items-center justify-between gap-2 ' +
              'cursor-grab active:cursor-grabbing ' +
              (isDragging ? 'opacity-40' : '')
            }>
      <span className="text-sm">
        <code className="font-mono text-xs text-ink-700">{f.name}</code>
        <span className="ml-2 text-ink-500 text-xs">{f.label}</span>
      </span>
      <span className="text-xs text-ink-400 font-mono">
        {f.arity === 0 ? '· now' : f.arity === 1 ? '(date)' : '(F,T)'}
        {' '}{f.return_type}
      </span>
    </button>
  );
}

//--- Standalone body: search + categorized list, no toggle button. Used by
//--- both the FieldPicker dropdown wrapper and the InsertPicker tabbed UI.
export function FieldPickerBody({ catalog, filter, onPick, autoFocus = true }: {
  catalog: FieldCatalog;
  filter?: (f: FieldDef) => boolean;
  onPick: (f: FieldDef) => void;
  autoFocus?: boolean;
}) {
  const [q, setQ] = useState('');
  const grouped = useMemo(() => {
    const fields = (catalog.fields || []).filter(f => !filter || filter(f));
    const filtered = q.trim()
      ? fields.filter(f =>
          f.name.toLowerCase().includes(q.toLowerCase()) ||
          f.label.toLowerCase().includes(q.toLowerCase()))
      : fields;
    const m = new Map<string, FieldDef[]>();
    for (const f of filtered) {
      const arr = m.get(f.category) ?? [];
      arr.push(f);
      m.set(f.category, arr);
    }
    return m;
  }, [catalog, filter, q]);

  return (
    <div className="flex flex-col">
      <div className="p-2 border-b border-ink-100 sticky top-0 bg-white z-10">
        <input className="input text-xs" autoFocus={autoFocus} placeholder="search fields…"
               value={q} onChange={e => setQ(e.target.value)} />
        <div className="text-[10px] text-ink-400 mt-1">click to insert, or drag onto a slot</div>
      </div>
      <div className="max-h-80 overflow-auto">
        {catalog.categories.map(cat => {
          const list = grouped.get(cat.id);
          if (!list || list.length === 0) return null;
          return (
            <div key={cat.id} className="border-b border-ink-50 last:border-0">
              <div className="px-3 py-1.5 text-[11px] uppercase font-semibold text-ink-500 bg-ink-50">{cat.id} · {cat.label}</div>
              <ul>
                {list.map(f => (
                  <li key={f.default_bucket ? `${f.name}__${f.default_bucket}` : f.name}>
                    <DraggableField f={f} onPick={onPick} />
                  </li>
                ))}
              </ul>
            </div>
          );
        })}
      </div>
    </div>
  );
}

export function FieldPicker({ catalog, filter, onPick, placeholder = 'pick field…', defaultOpen = false }: WrapperProps) {
  const [open, setOpen] = useState(defaultOpen);
  const pickAndClose = (f: FieldDef) => { onPick(f); setOpen(false); };

  return (
    <div className="relative inline-block">
      <button type="button" className="btn-secondary text-xs" onClick={() => setOpen(o => !o)}>
        {open ? 'Close' : placeholder}
      </button>
      {open && (
        <div className="absolute z-20 mt-1 w-96 bg-white border border-ink-200 rounded shadow-lg overflow-hidden">
          <FieldPickerBody catalog={catalog} filter={filter} onPick={pickAndClose} />
        </div>
      )}
    </div>
  );
}
