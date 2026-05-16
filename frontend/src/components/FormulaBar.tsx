import { useState } from 'react';
import { useDroppable } from '@dnd-kit/core';
import type { Column, FieldCatalog, FieldDef, Predicate } from '../types';
import type { Chip, ChipOp } from '../lib/exprChips';
import { astToChips, astToText, chipsToAst, newChipId } from '../lib/exprChips';
import { FieldPickerBody } from './FieldPicker';
import { PredicateEditor } from './PredicateEditor';
import { BlueprintPicker } from './BlueprintPicker';

type Props = {
  chips: Chip[];
  onChipsChange: (next: Chip[]) => void;
  catalog: FieldCatalog;
  dateParams: string[];
  path: string;             // base drop-zone id, e.g. "col:0"
  refCandidates: Column[];  // previously-defined columns (numeric/formula) — pickable as @col_key refs
};

const numericOnly = (f: { return_type: string }) =>
  f.return_type === 'money' || f.return_type === 'pct' ||
  f.return_type === 'int'   || f.return_type === 'date';

function makeFieldChip(f: FieldDef, dateParams: string[]): Chip {
  return {
    id: newChipId(),
    kind: 'field',
    name: f.name,
    args:
      f.arity === 1 ? [dateParams[0] ?? ''] :
      f.arity === 2 ? [dateParams[0] ?? '', dateParams[1] ?? dateParams[0] ?? ''] :
                      [],
  };
}

export function FormulaBar({ chips, onChipsChange, catalog, dateParams, path, refCandidates }: Props) {
  const insertAt = (idx: number, chip: Chip) => {
    const next = chips.slice();
    next.splice(idx, 0, chip);
    onChipsChange(next);
  };
  const insertManyAt = (idx: number, items: Chip[]) => {
    if (items.length === 0) return;
    const next = chips.slice();
    next.splice(idx, 0, ...items);
    onChipsChange(next);
  };
  const replaceAt = (idx: number, chip: Chip) => {
    const next = chips.slice();
    next[idx] = chip;
    onChipsChange(next);
  };
  const replaceManyAt = (idx: number, items: Chip[]) => {
    const next = chips.slice();
    next.splice(idx, 1, ...items);
    onChipsChange(next);
  };
  const deleteAt = (idx: number) => {
    const next = chips.slice();
    next.splice(idx, 1);
    onChipsChange(next);
  };

  return (
    <div className="flex items-center flex-wrap gap-0.5 p-2 bg-white border border-ink-200 rounded min-h-[3rem]">
      <InsertionSlot index={0} path={path} catalog={catalog} dateParams={dateParams}
                     refCandidates={refCandidates}
                     onInsert={(c) => insertAt(0, c)}
                     onInsertMany={(cs) => insertManyAt(0, cs)} />
      {chips.map((c, i) => (
        <span key={c.id} className="flex items-center">
          <ChipView chip={c} catalog={catalog} dateParams={dateParams}
                    refCandidates={refCandidates}
                    onChange={(next) => replaceAt(i, next)}
                    onReplaceMany={(items) => replaceManyAt(i, items)}
                    onDelete={() => deleteAt(i)} />
          <InsertionSlot index={i + 1} path={path} catalog={catalog} dateParams={dateParams}
                         refCandidates={refCandidates}
                         onInsert={(c2) => insertAt(i + 1, c2)}
                         onInsertMany={(cs) => insertManyAt(i + 1, cs)} />
        </span>
      ))}
      {chips.length === 0 && (
        <span className="text-xs text-ink-400 ml-2">Click + to insert a field, number, operator, or parenthesis</span>
      )}
    </div>
  );
}

//--- Insertion slot between chips: button + popover + drop target -----

function InsertionSlot({ index, path, catalog, dateParams, refCandidates, onInsert, onInsertMany }: {
  index: number;
  path: string;
  catalog: FieldCatalog;
  dateParams: string[];
  refCandidates: Column[];
  onInsert: (c: Chip) => void;
  onInsertMany: (cs: Chip[]) => void;
}) {
  void onInsertMany;
  const [open, setOpen] = useState(false);
  const dropId = `${path}:slot:${index}`;
  const { setNodeRef, isOver } = useDroppable({ id: dropId });

  return (
    <span ref={setNodeRef} className="relative inline-flex">
      <button type="button"
              className={
                'w-5 h-6 text-ink-400 hover:text-ink-700 hover:bg-ink-100 rounded text-xs ' +
                (isOver ? 'bg-blue-200 text-blue-900 ring-2 ring-blue-400' : '')
              }
              title="insert here"
              onClick={() => setOpen(o => !o)}>+</button>
      {open && (
        <span className="absolute z-30 top-7 left-0 w-96 bg-white border border-ink-200 rounded shadow-lg overflow-hidden"
              onMouseDown={e => e.stopPropagation()}>
          <InsertPicker
            catalog={catalog}
            dateParams={dateParams}
            refCandidates={refCandidates}
            onInsert={(c) => { onInsert(c); setOpen(false); }}
            onClose={() => setOpen(false)}
          />
        </span>
      )}
    </span>
  );
}

//--- Chip render -----------------------------------------------------

function ChipView({ chip, catalog, dateParams, refCandidates, onChange, onReplaceMany, onDelete }: {
  chip: Chip;
  catalog: FieldCatalog;
  dateParams: string[];
  refCandidates: Column[];
  onChange: (next: Chip) => void;
  onReplaceMany: (items: Chip[]) => void;
  onDelete: () => void;
}) {
  if (chip.kind === 'blueprint') {
    return (
      <BlueprintChipView chip={chip}
                         onDetach={() => onReplaceMany(chip.inner)}
                         onDelete={onDelete} />
    );
  }

  if (chip.kind === 'col_ref') {
    const col = refCandidates.find(c => c.key === chip.key);
    const stale = !col;
    return (
      <span className={
        'inline-flex items-center px-2 py-1 rounded border font-mono text-xs ' +
        (stale
          ? 'border-red-300 bg-red-50 text-red-800'
          : 'border-violet-300 bg-violet-50 text-violet-900')
      }
            title={stale ? `Unknown column key '${chip.key}'` : `Reference to column ${col!.label}`}>
        <span className="select-none">@</span>{chip.key}
        {col && <span className="ml-1 text-[10px] text-violet-600 select-none">{col.label}</span>}
        <DeleteX onClick={onDelete} />
      </span>
    );
  }

  if (chip.kind === 'op') {
    return (
      <span className="inline-flex items-center px-1 py-1 rounded border border-amber-200 bg-amber-50 font-mono text-base text-amber-900">
        <select className="bg-transparent border-none focus:ring-0 text-base px-1"
                value={chip.op}
                onChange={e => onChange({ ...chip, op: e.target.value as ChipOp })}>
          <option value="+">+</option>
          <option value="-">−</option>
          <option value="*">×</option>
          <option value="/">÷</option>
        </select>
        <DeleteX onClick={onDelete} />
      </span>
    );
  }

  if (chip.kind === 'lparen' || chip.kind === 'rparen') {
    return (
      <span className="inline-flex items-center px-2 py-1 rounded border border-ink-300 bg-ink-50 font-mono text-base text-ink-700">
        {chip.kind === 'lparen' ? '(' : ')'}
        <DeleteX onClick={onDelete} />
      </span>
    );
  }

  if (chip.kind === 'literal') {
    return (
      <span className="inline-flex items-center px-1 py-1 rounded border border-slate-300 bg-slate-50">
        <span className="text-xs text-ink-500 font-mono mr-1">num</span>
        <input className="w-20 text-sm border-none bg-transparent focus:ring-0 px-1 tabular-nums"
               type="number" step="any" value={chip.value}
               onChange={e => onChange({ ...chip, value: Number(e.target.value) })} />
        <DeleteX onClick={onDelete} />
      </span>
    );
  }

  // field
  const f = catalog.fields.find(x => x.name === chip.name);
  return (
    <FieldChipView chip={chip} field={f} catalog={catalog} dateParams={dateParams}
                   onChange={onChange} onDelete={onDelete} />
  );
}

function FieldChipView({ chip, field, catalog, dateParams, onChange, onDelete }: {
  chip:       Extract<Chip, { kind: 'field' }>;
  field?:     FieldDef;
  catalog:    FieldCatalog;
  dateParams: string[];
  onChange:   (next: Chip) => void;
  onDelete:   () => void;
}) {
  const [filterOpen, setFilterOpen] = useState(false);
  const cmpCount = countCmps(chip.predicate);
  const supportsFilter = !!(field?.supports_predicate);
  const filterable = field
    ? (catalog.filterable_by_source?.[field.source] ?? [])
    : [];

  const setPredicate = (p: Predicate | null) => {
    const next = { ...chip };
    if (p) next.predicate = p;
    else   delete (next as any).predicate;
    onChange(next);
  };

  return (
    <span className="inline-flex items-center px-2 py-1 rounded border border-emerald-300 bg-emerald-50 relative">
      <code className="font-mono text-xs text-emerald-900">{chip.name}</code>
      {chip.args.length > 0 && (
        <span className="text-xs text-emerald-700 ml-1 flex items-center gap-0.5">
          (
          {chip.args.map((arg, i) => (
            <span key={i} className="flex items-center">
              {i > 0 && <span>,</span>}
              <select className="bg-transparent border border-emerald-200 rounded text-xs px-1 ml-0.5"
                      value={arg}
                      onChange={e => {
                        const args = chip.args.slice();
                        args[i] = e.target.value;
                        onChange({ ...chip, args });
                      }}>
                {dateParams.length === 0 && <option value="">(no date params)</option>}
                {dateParams.map(d => <option key={d} value={d}>{d}</option>)}
              </select>
            </span>
          ))}
          )
        </span>
      )}
      {field && (
        <span className="text-[10px] text-emerald-600 ml-1 font-mono select-none" title={field.label}>
          {field.return_type}
        </span>
      )}
      {supportsFilter && (
        <button type="button"
                className={
                  'text-[11px] ml-1 px-1.5 py-0.5 rounded font-mono border ' +
                  (cmpCount > 0
                    ? 'bg-amber-100 border-amber-300 text-amber-900 hover:bg-amber-200'
                    : 'bg-white border-emerald-300 text-emerald-700 hover:bg-emerald-100')
                }
                onClick={() => setFilterOpen(o => !o)}>
          {cmpCount > 0 ? `▼ ${cmpCount}` : '+ filter'}
        </button>
      )}
      <DeleteX onClick={onDelete} />

      {filterOpen && field && (
        <div className="absolute z-40 top-full left-0 mt-1 w-[520px] bg-white border border-ink-200 rounded shadow-lg p-3"
             onMouseDown={e => e.stopPropagation()}>
          <div className="flex items-center justify-between mb-2">
            <div className="text-xs font-semibold text-ink-700">Row filter for {chip.name}</div>
            <button type="button" className="text-xs text-ink-500 hover:text-ink-900" onClick={() => setFilterOpen(false)}>close ×</button>
          </div>
          <PredicateEditor
            source={field.source as any}
            filterable={filterable}
            predicate={chip.predicate ?? null}
            onChange={setPredicate}
          />
        </div>
      )}
    </span>
  );
}

function countCmps(p: Predicate | null | undefined): number {
  if (!p) return 0;
  if (p.kind === 'cmp') return 1;
  if (p.kind === 'not') return countCmps(p.item);
  return p.items.reduce((n, c) => n + countCmps(c), 0);
}

function DeleteX({ onClick }: { onClick: () => void }) {
  return (
    <button type="button"
            className="text-red-500 hover:text-red-700 text-xs ml-1 px-0.5"
            title="remove"
            onClick={onClick}>×</button>
  );
}

//--- Tabbed insert picker used inside the `+` popover. Field tab is the
//--- default and most common path; Col / Blueprint / Math are alongside it.
//--- One rectangular surface, no nested dropdowns, ink-palette styling.
type InsertTab = 'field' | 'column' | 'blueprint' | 'math';

function InsertPicker({ catalog, dateParams, refCandidates, onInsert, onClose }: {
  catalog: FieldCatalog;
  dateParams: string[];
  refCandidates: Column[];
  onInsert: (c: Chip) => void;
  onClose: () => void;
}) {
  const [tab, setTab] = useState<InsertTab>('field');

  const TabBtn = ({ id, label }: { id: InsertTab; label: string }) => (
    <button type="button"
            onClick={() => setTab(id)}
            className={
              'text-xs px-2.5 py-1 rounded ' +
              (tab === id
                ? 'bg-ink-900 text-white'
                : 'bg-ink-100 text-ink-700 hover:bg-ink-200')
            }>
      {label}
    </button>
  );

  return (
    <div className="flex flex-col">
      {/* Tab bar */}
      <div className="flex items-center gap-1 p-2 border-b border-ink-100 bg-ink-50/40">
        <TabBtn id="field"     label="Field" />
        <TabBtn id="column"    label="Col" />
        <TabBtn id="blueprint" label="Blueprint" />
        <TabBtn id="math"      label="Math" />
        <button type="button" className="text-xs text-ink-500 hover:text-ink-900 px-1 ml-auto"
                onClick={onClose}>
          esc
        </button>
      </div>

      {/* Active tab body */}
      {tab === 'field' && (
        <FieldPickerBody
          catalog={catalog}
          filter={numericOnly}
          onPick={(f) => onInsert(makeFieldChip(f, dateParams))}
        />
      )}

      {tab === 'column' && (
        <ColumnTabBody
          columns={refCandidates}
          onPick={(col) => onInsert({ id: newChipId(), kind: 'col_ref', key: col.key })}
        />
      )}

      {tab === 'blueprint' && (
        <div className="p-2 max-h-80 overflow-auto">
          <BlueprintPicker
            templateDateParams={dateParams}
            onInsert={(expr, name) => {
              //--- One blueprint chip with name + inner chips so the bar
              //--- shows the name instead of the expanded expression.
              onInsert({ id: newChipId(), kind: 'blueprint', name, inner: astToChips(expr) });
            }}
          />
        </div>
      )}

      {tab === 'math' && (
        <MathTabBody onInsert={onInsert} />
      )}
    </div>
  );
}

//--- Column tab body: list of refCandidates as @col_key rows. Same row shape
//--- as field rows so the visual rhythm stays consistent across tabs.
function ColumnTabBody({ columns, onPick }: { columns: Column[]; onPick: (c: Column) => void }) {
  if (columns.length === 0) {
    return (
      <div className="px-3 py-6 text-[11px] text-ink-400 italic text-center">
        No earlier columns to reference yet.
        <div className="text-[10px] text-ink-400 mt-1 not-italic">
          Define an identifier or numeric column above this one first.
        </div>
      </div>
    );
  }
  return (
    <div className="max-h-80 overflow-auto">
      <div className="px-3 py-1.5 text-[11px] uppercase font-semibold text-ink-500 bg-ink-50 border-b border-ink-100 sticky top-0">
        Columns in this template
      </div>
      {columns.map(col => (
        <button key={col.key} type="button"
                onClick={() => onPick(col)}
                className="w-full text-left px-3 py-1.5 hover:bg-ink-50 flex items-center justify-between gap-2 border-b border-ink-50 last:border-0">
          <span className="text-sm">
            <code className="font-mono text-xs text-violet-700">@{col.key}</code>
            <span className="ml-2 text-ink-500 text-xs">{col.label}</span>
          </span>
          <span className="text-xs text-ink-400 font-mono">· {col.kind}</span>
        </button>
      ))}
    </div>
  );
}

//--- Math tab body: keypad-style grid of number / operators / parens.
//--- Bigger square buttons feel calmer than the cramped inline strip.
function MathTabBody({ onInsert }: { onInsert: (c: Chip) => void }) {
  const Key = ({ label, onClick, mono = true }: { label: string; onClick: () => void; mono?: boolean }) => (
    <button type="button" onClick={onClick}
            className={
              'h-10 rounded border border-ink-200 bg-white hover:bg-ink-50 text-ink-900 text-sm ' +
              (mono ? 'font-mono' : '')
            }>
      {label}
    </button>
  );
  const ops: { sym: string; op: ChipOp }[] = [
    { sym: '+', op: '+' }, { sym: '−', op: '-' }, { sym: '×', op: '*' }, { sym: '÷', op: '/' },
  ];
  return (
    <div className="p-3 space-y-2">
      <div className="text-[11px] uppercase font-semibold text-ink-500">Numbers · grouping</div>
      <div className="grid grid-cols-3 gap-2">
        <Key label="123" mono={false} onClick={() => onInsert({ id: newChipId(), kind: 'literal', value: 0 })} />
        <Key label="("  onClick={() => onInsert({ id: newChipId(), kind: 'lparen' })} />
        <Key label=")"  onClick={() => onInsert({ id: newChipId(), kind: 'rparen' })} />
      </div>
      <div className="text-[11px] uppercase font-semibold text-ink-500 pt-1">Operators</div>
      <div className="grid grid-cols-4 gap-2">
        {ops.map(({ sym, op }) => (
          <Key key={op} label={sym}
               onClick={() => onInsert({ id: newChipId(), kind: 'op', op })} />
        ))}
      </div>
    </div>
  );
}

//--- Blueprint chip: single named pill. Click toggles an inline read-only
//--- preview of the inner formula; "Detach" replaces the pill with its
//--- constituent chips for direct editing.
function BlueprintChipView({ chip, onDetach, onDelete }: {
  chip: Extract<Chip, { kind: 'blueprint' }>;
  onDetach: () => void;
  onDelete: () => void;
}) {
  const [open, setOpen] = useState(false);
  let innerText = '';
  try { innerText = astToText(chipsToAst(chip.inner)); } catch { innerText = '(unrenderable)'; }
  return (
    <span className="relative inline-flex items-center px-2 py-1 rounded border border-indigo-300 bg-indigo-50">
      <button type="button"
              className="text-xs font-medium text-indigo-900 hover:text-indigo-950 flex items-center"
              title="Click to view the blueprint's inner formula"
              onClick={() => setOpen(o => !o)}>
        <span className="text-[10px] text-indigo-500 mr-1 select-none">{open ? '▾' : '▸'}</span>
        <span className="text-[10px] uppercase tracking-wide text-indigo-500 mr-1 select-none">bp</span>
        {chip.name}
      </button>
      <DeleteX onClick={onDelete} />
      {open && (
        <div className="absolute z-40 top-full left-0 mt-1 min-w-[320px] max-w-[600px] bg-white border border-indigo-200 rounded shadow-lg p-3 space-y-2"
             onMouseDown={e => e.stopPropagation()}>
          <div className="flex items-center justify-between">
            <div className="text-xs font-semibold text-ink-700">Blueprint · {chip.name}</div>
            <button type="button" className="text-xs text-ink-500 hover:text-ink-900"
                    onClick={() => setOpen(false)}>close ×</button>
          </div>
          <div className="text-[11px] text-ink-500">Inner formula (read-only):</div>
          <code className="block font-mono text-xs text-ink-800 bg-ink-50 px-2 py-1.5 rounded break-all">
            {innerText}
          </code>
          <div className="flex items-center justify-end pt-1">
            <button type="button"
                    className="btn-secondary text-xs px-2 py-1"
                    title="Replace this pill with its constituent chips so you can edit them inline"
                    onClick={() => { setOpen(false); onDetach(); }}>
              Detach into chips
            </button>
          </div>
        </div>
      )}
    </span>
  );
}
