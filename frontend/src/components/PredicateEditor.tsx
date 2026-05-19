import { useState } from 'react';
import type {
  FilterableField, FilterOp, Predicate, PredCmp, PredAnd, PredOr, PredNot,
} from '../types';

type Props = {
  source:     'deal' | 'daily' | 'position' | 'order_open' | 'order_hist' | 'user';
  filterable: FilterableField[];
  predicate:  Predicate | null;
  onChange:   (next: Predicate | null) => void;
};

const TEXT_OPS: FilterOp[]  = ['eq', 'neq', 'contains', 'startswith', 'endswith', 'regex', 'in'];
const NUM_OPS:  FilterOp[]  = ['eq', 'neq', 'lt', 'lte', 'gt', 'gte', 'in'];
const ENUM_OPS: FilterOp[]  = ['eq', 'neq', 'in'];

const OP_LABEL: Record<FilterOp, string> = {
  eq: '=', neq: '≠', lt: '<', lte: '≤', gt: '>', gte: '≥',
  //--- "~ regex / glob" reflects that the backend accepts either dialect
  //--- under the same op — a regex `^GANN-.*` and a glob `GANN-*` both work.
  regex: '~ regex / glob', glob: 'glob', contains: 'contains', startswith: 'starts with', endswith: 'ends with', in: 'in [list]',
};

function opsForField(f?: FilterableField): FilterOp[] {
  if (!f) return TEXT_OPS;
  if (f.type === 'num')  return NUM_OPS;
  if (f.type === 'enum') return ENUM_OPS;
  return TEXT_OPS;
}

function defaultValueForField(f?: FilterableField): PredCmp['value'] {
  if (!f) return '';
  if (f.type === 'enum') return f.enum_values && f.enum_values.length > 0 ? f.enum_values[0].code : 0;
  if (f.type === 'num')  return 0;
  return '';
}

function mkCmp(filterable: FilterableField[]): PredCmp {
  const first = filterable[0];
  return {
    kind: 'cmp',
    field: first?.name ?? '',
    op:    opsForField(first)[0],
    value: defaultValueForField(first),
  };
}

function countCmps(p: Predicate | null | undefined): number {
  if (!p) return 0;
  if (p.kind === 'cmp') return 1;
  if (p.kind === 'not') return countCmps(p.item);
  return p.items.reduce((n, c) => n + countCmps(c), 0);
}

export function PredicateEditor({ source, filterable, predicate, onChange }: Props) {
  const reset = () => onChange(null);

  const addToTop = () => {
    const fresh = mkCmp(filterable);
    if (!predicate) { onChange(fresh); return; }
    if (predicate.kind === 'and') { onChange({ ...predicate, items: [...predicate.items, fresh] }); return; }
    if (predicate.kind === 'or')  { onChange({ ...predicate, items: [...predicate.items, fresh] }); return; }
    //--- wrap current in AND, append
    onChange({ kind: 'and', items: [predicate, fresh] });
  };

  return (
    <div className="space-y-2">
      <div className="text-[11px] text-ink-500">
        Source: <code className="font-mono">{source}</code> · filters apply to each row before aggregating.
      </div>

      {!predicate ? (
        <div>
          <button type="button" className="btn-secondary text-xs" onClick={addToTop}>+ Add condition</button>
        </div>
      ) : (
        <PredicateNode
          predicate={predicate}
          filterable={filterable}
          depth={0}
          onChange={p => onChange(p)}
          onDelete={reset}
        />
      )}

      {predicate && (
        <div className="flex items-center gap-2">
          <button type="button" className="btn-secondary text-xs" onClick={addToTop}>+ Condition</button>
          <button type="button" className="btn-secondary text-xs text-red-600" onClick={reset}>Clear all</button>
          <span className="text-[11px] text-ink-400 ml-auto">{countCmps(predicate)} condition{countCmps(predicate) === 1 ? '' : 's'}</span>
        </div>
      )}
      <div className="text-[10px] text-ink-400 leading-relaxed pt-1">
        Tip: use the <span className="font-mono">¬</span> button to negate a single condition or a whole group.
        Two equivalent ways to exclude DT and WT:
        <br />
        &nbsp;&nbsp;• <span className="font-mono">¬ comment startswith "DT" AND ¬ comment startswith "WT"</span>
        <br />
        &nbsp;&nbsp;• <span className="font-mono">¬ ( comment startswith "DT" OR comment startswith "WT" )</span>
      </div>
    </div>
  );
}

//+--- Recursive node renderer --------------------------------------+

function PredicateNode({ predicate, filterable, depth, onChange, onDelete }: {
  predicate:  Predicate;
  filterable: FilterableField[];
  depth:      number;
  onChange:   (next: Predicate) => void;
  onDelete:   () => void;
}) {
  //--- Inline-render `Not{Cmp}` and `Not{Group}` as a negated row/group with
  //--- a visible ¬ toggle. The user reads "NOT comment startswith DT" without
  //--- a nested box. The underlying AST shape stays as Not{...}.
  if (predicate.kind === 'not' && predicate.item.kind === 'cmp') {
    return (
      <CmpRow predicate={predicate.item} filterable={filterable} depth={depth}
              negated={true}
              onChange={(newCmp) => onChange({ kind: 'not', item: newCmp as PredCmp })}
              onToggleNegate={() => onChange(predicate.item)}
              onDelete={onDelete} />
    );
  }
  if (predicate.kind === 'not' && (predicate.item.kind === 'and' || predicate.item.kind === 'or')) {
    const inner = predicate.item;
    return (
      <GroupRow predicate={inner} filterable={filterable} depth={depth}
                negated={true}
                onChange={(newGroup) => onChange({ kind: 'not', item: newGroup as PredAnd | PredOr })}
                onToggleNegate={() => onChange(inner)}
                onDelete={onDelete} />
    );
  }
  if (predicate.kind === 'cmp') {
    return (
      <CmpRow predicate={predicate} filterable={filterable} depth={depth}
              negated={false}
              onChange={onChange}
              onToggleNegate={() => onChange({ kind: 'not', item: predicate })}
              onDelete={onDelete} />
    );
  }
  if (predicate.kind === 'not') {
    //--- Rare: Not{Not}. Fall back to the boxed NotRow.
    return (
      <NotRow predicate={predicate} filterable={filterable}
              onChange={onChange} onDelete={onDelete} depth={depth} />
    );
  }
  //--- AND / OR group (not wrapped in NOT — render normally).
  return (
    <GroupRow predicate={predicate} filterable={filterable} depth={depth}
              negated={false}
              onChange={onChange}
              onToggleNegate={() => onChange({ kind: 'not', item: predicate })}
              onDelete={onDelete} />
  );
}

//+--- Cmp leaf -----------------------------------------------------+

function CmpRow({ predicate, filterable, onChange, onDelete, depth, negated, onToggleNegate }: {
  predicate:  PredCmp;
  filterable: FilterableField[];
  onChange:   (next: Predicate) => void;
  onDelete:   () => void;
  depth:      number;
  negated:    boolean;
  onToggleNegate: () => void;
}) {
  const field = filterable.find(f => f.name === predicate.field);
  const ops   = opsForField(field);

  const onFieldChange = (name: string) => {
    const nf = filterable.find(f => f.name === name);
    const newOps = opsForField(nf);
    onChange({
      ...predicate,
      field: name,
      op: newOps.includes(predicate.op) ? predicate.op : newOps[0],
      value: defaultValueForField(nf),
    });
  };

  const onOpChange = (op: FilterOp) => {
    //--- moving to/from 'in' resets value type
    if (op === 'in' && !Array.isArray(predicate.value))
      onChange({ ...predicate, op, value: [] as any });
    else if (op !== 'in' && Array.isArray(predicate.value))
      onChange({ ...predicate, op, value: defaultValueForField(field) });
    else
      onChange({ ...predicate, op });
  };

  return (
    <div className="flex items-center gap-1 flex-wrap" style={{ marginLeft: depth * 12 }}>
      <NegateToggle active={negated} onClick={onToggleNegate} />
      <select className="input text-xs" style={{ width: 150 }}
              value={predicate.field}
              onChange={e => onFieldChange(e.target.value)}>
        {filterable.length === 0 && <option value="">(no fields)</option>}
        {filterable.map(f => (
          <option key={f.name} value={f.name}>{f.label} ({f.type})</option>
        ))}
      </select>

      <select className="input text-xs" style={{ width: 110 }}
              value={predicate.op}
              onChange={e => onOpChange(e.target.value as FilterOp)}>
        {ops.map(op => <option key={op} value={op}>{OP_LABEL[op]}</option>)}
      </select>

      <ValueEditor predicate={predicate} field={field} onChange={onChange} />

      <NodeActionMenu predicate={predicate} onChange={onChange} onDelete={onDelete} />
    </div>
  );
}

//--- Compact NOT toggle pill used by CmpRow and GroupRow headers.
function NegateToggle({ active, onClick }: { active: boolean; onClick: () => void }) {
  return (
    <button type="button"
            title={active ? 'Remove NOT' : 'Negate this condition'}
            className={
              'text-[11px] px-1.5 py-0.5 rounded font-mono border transition-colors ' +
              (active
                ? 'bg-red-100 border-red-300 text-red-800 hover:bg-red-200'
                : 'bg-white border-ink-300 text-ink-500 hover:bg-ink-50 hover:text-ink-800')
            }
            onClick={onClick}>
      {active ? '¬ NOT' : '¬'}
    </button>
  );
}

function ValueEditor({ predicate, field, onChange }: {
  predicate: PredCmp;
  field?:    FilterableField;
  onChange:  (next: Predicate) => void;
}) {
  if (predicate.op === 'in') {
    const list = Array.isArray(predicate.value) ? predicate.value : [];
    return (
      <InListEditor field={field} list={list as any}
                    onChange={(next) => onChange({ ...predicate, value: next as any })} />
    );
  }
  if (field?.type === 'enum') {
    return (
      <select className="input text-xs" style={{ width: 160 }}
              value={String(predicate.value)}
              onChange={e => onChange({ ...predicate, value: Number(e.target.value) })}>
        {(field.enum_values ?? []).map(ev => (
          <option key={ev.code} value={ev.code}>{ev.label}</option>
        ))}
      </select>
    );
  }
  if (field?.type === 'num') {
    return (
      <input className="input text-xs tabular-nums" style={{ width: 120 }}
             type="number" step="any" value={Number(predicate.value)}
             onChange={e => onChange({ ...predicate, value: Number(e.target.value) })} />
    );
  }
  //--- text
  return (
    <input className="input text-xs font-mono" style={{ width: 200 }}
           type="text" value={String(predicate.value)}
           onChange={e => onChange({ ...predicate, value: e.target.value })}
           placeholder="value" />
  );
}

function InListEditor({ field, list, onChange }: {
  field?:   FilterableField;
  list:     (string | number)[];
  onChange: (next: (string | number)[]) => void;
}) {
  const [draft, setDraft] = useState('');
  const add = () => {
    const t = draft.trim();
    if (!t) return;
    if (field?.type === 'num') {
      const n = Number(t);
      if (Number.isFinite(n) && !list.includes(n)) onChange([...list, n]);
    } else if (field?.type === 'enum') {
      const ev = (field.enum_values ?? []).find(x => x.label === t || String(x.code) === t);
      if (ev && !list.includes(ev.code)) onChange([...list, ev.code]);
    } else {
      if (!list.includes(t)) onChange([...list, t]);
    }
    setDraft('');
  };
  const removeAt = (idx: number) => onChange(list.filter((_, i) => i !== idx));

  return (
    <span className="inline-flex items-center gap-1 flex-wrap border border-ink-200 rounded px-1 py-0.5 bg-white">
      {list.map((v, i) => (
        <span key={i} className="text-xs px-1.5 py-0.5 bg-ink-100 rounded font-mono">
          {field?.type === 'enum' ? (field.enum_values?.find(e => e.code === v)?.label ?? String(v)) : String(v)}
          <button type="button" className="text-red-500 ml-1" onClick={() => removeAt(i)}>×</button>
        </span>
      ))}
      <input className="text-xs px-1 py-0.5 outline-none w-20" value={draft}
             onChange={e => setDraft(e.target.value)}
             onKeyDown={e => { if (e.key === 'Enter') { e.preventDefault(); add(); } }}
             placeholder="add…" />
    </span>
  );
}

//+--- Group (And/Or) -----------------------------------------------+

function GroupRow({ predicate, filterable, onChange, onDelete, depth, negated, onToggleNegate }: {
  predicate:  PredAnd | PredOr;
  filterable: FilterableField[];
  onChange:   (next: Predicate) => void;
  onDelete:   () => void;
  depth:      number;
  negated:    boolean;
  onToggleNegate: () => void;
}) {
  const setItem = (idx: number, next: Predicate) => {
    const items = predicate.items.slice();
    items[idx] = next;
    onChange({ ...predicate, items });
  };
  const removeItem = (idx: number) => {
    const items = predicate.items.filter((_, i) => i !== idx);
    if (items.length === 0) onDelete();
    else if (items.length === 1) onChange(items[0]);     // unwrap singleton group
    else onChange({ ...predicate, items });
  };
  const addItem = () => onChange({ ...predicate, items: [...predicate.items, mkCmp(filterable)] });

  const toggleConnector = () => onChange({
    ...predicate,
    kind: predicate.kind === 'and' ? 'or' : 'and',
  } as Predicate);

  return (
    <div className={
           'border-l-2 pl-2 space-y-1 ' +
           (negated ? 'border-red-300 bg-red-50/30 rounded' : 'border-ink-200')
         }
         style={{ marginLeft: depth * 12 }}>
      <div className="flex items-center gap-2 mb-1">
        <NegateToggle active={negated} onClick={onToggleNegate} />
        <button type="button" className="text-xs px-2 py-0.5 rounded bg-ink-100 hover:bg-ink-200 font-mono"
                onClick={toggleConnector}>
          {predicate.kind === 'and' ? 'AND' : 'OR'} ⇄
        </button>
        <span className="text-[11px] text-ink-400">
          {negated ? 'negated group' : 'group'} · {predicate.items.length} items
        </span>
        <span className="ml-auto"><NodeActionMenu predicate={predicate} onChange={onChange} onDelete={onDelete} /></span>
      </div>
      {predicate.items.map((it, idx) => (
        <PredicateNode key={idx} predicate={it} filterable={filterable} depth={depth + 1}
                       onChange={n => setItem(idx, n)}
                       onDelete={() => removeItem(idx)} />
      ))}
      <div className="flex items-center gap-2 mt-1">
        <button type="button" className="btn-secondary text-xs px-2 py-0.5" onClick={addItem}>+ Condition</button>
      </div>
    </div>
  );
}

//+--- NOT wrapper --------------------------------------------------+

function NotRow({ predicate, filterable, onChange, onDelete, depth }: {
  predicate:  PredNot;
  filterable: FilterableField[];
  onChange:   (next: Predicate) => void;
  onDelete:   () => void;
  depth:      number;
}) {
  const setInner = (next: Predicate) => onChange({ ...predicate, item: next });
  const unwrap   = () => onChange(predicate.item);

  return (
    <div className="border-l-2 border-red-300 pl-2 space-y-1" style={{ marginLeft: depth * 12 }}>
      <div className="flex items-center gap-2 mb-1">
        <span className="text-xs px-2 py-0.5 rounded bg-red-100 text-red-800 font-mono">NOT</span>
        <button type="button" className="btn-secondary text-xs px-2 py-0.5" onClick={unwrap}>Unwrap</button>
        <span className="ml-auto"><NodeActionMenu predicate={predicate} onChange={onChange} onDelete={onDelete} /></span>
      </div>
      <PredicateNode predicate={predicate.item} filterable={filterable} depth={depth + 1}
                     onChange={setInner}
                     onDelete={onDelete} />
    </div>
  );
}

//+--- Action menu (wrap in NOT / AND / OR, delete) -----------------+

function NodeActionMenu({ predicate, onChange, onDelete }: {
  predicate: Predicate;
  onChange:  (next: Predicate) => void;
  onDelete:  () => void;
}) {
  const [open, setOpen] = useState(false);
  const wrapNot = () => { onChange({ kind: 'not', item: predicate }); setOpen(false); };
  const wrapAnd = () => { onChange({ kind: 'and', items: [predicate] }); setOpen(false); };
  const wrapOr  = () => { onChange({ kind: 'or',  items: [predicate] }); setOpen(false); };

  return (
    <span className="relative inline-flex">
      <button type="button" className="text-xs px-1 text-ink-500 hover:text-ink-900"
              onClick={() => setOpen(o => !o)}>⋯</button>
      {open && (
        <span className="absolute z-30 top-5 right-0 bg-white border border-ink-200 rounded shadow-lg p-1 flex flex-col text-xs min-w-[120px]">
          <button type="button" className="text-left px-2 py-1 hover:bg-ink-50" onClick={wrapAnd}>Wrap in AND</button>
          <button type="button" className="text-left px-2 py-1 hover:bg-ink-50" onClick={wrapOr}>Wrap in OR</button>
          <button type="button" className="text-left px-2 py-1 hover:bg-ink-50" onClick={wrapNot}>Wrap in NOT</button>
          <button type="button" className="text-left px-2 py-1 hover:bg-red-50 text-red-600" onClick={() => { onDelete(); setOpen(false); }}>Delete</button>
        </span>
      )}
    </span>
  );
}
