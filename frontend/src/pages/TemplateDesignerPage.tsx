import { useEffect, useMemo, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { DndContext, DragOverlay, PointerSensor, useSensor, useSensors,
         type DragEndEvent, type DragStartEvent } from '@dnd-kit/core';
import { TemplatesAPI } from '../api/templates';
import { loadCatalogWithBuckets } from '../lib/catalogWithBuckets';
import type {
  Column, ColumnFormat, ColumnKind, ExprNode, FieldCatalog, FieldDef, Predicate,
  SortSpec, Template, TemplateInput, ValidationError,
} from '../types';
import { FormulaEditor } from '../components/FormulaEditor';
import { FieldPicker } from '../components/FieldPicker';
import { PredicateEditor } from '../components/PredicateEditor';
import { Breadcrumbs } from '../components/Breadcrumbs';
import {
  formatForReturnType, inferFormulaFormat,
  reconcileIdentifierFormats, suggestedFormat,
} from '../lib/columnFormat';

//--- Predicate target for a pivot identifier — mirrors Engine.cpp's
//--- driver split. symbol/ticket live on deal records; everything else
//--- (login, group, name, country, comment, currency, …) is user-source.
function predicateSourceFor(source: string | undefined): 'user' | 'deal' {
  if (source === 'symbol' || source === 'ticket') return 'deal';
  return 'user';
}

function countPredicate(p: Predicate | null | undefined): number {
  if(!p) return 0;
  if(p.kind === 'cmp') return 1;
  if(p.kind === 'not') return countPredicate(p.item);
  return p.items.reduce((n, c) => n + countPredicate(c), 0);
}
import { BlueprintsAPI } from '../api/blueprints';
import {
  astToChips, chipsToAst, newChipId,
  type Chip,
} from '../lib/exprChips';

const emptyTemplate: TemplateInput = {
  name: '',
  description: '',
  row_model: 'per_account',
  date_params: ['date_from', 'date_to'],
  columns: [],
  sort: { column_key: '', direction: 'desc' },
  default_top_n: 0,
};

function uniqueKey(prefix: string, cols: Column[]) {
  let i = 1;
  while (cols.some(c => c.key === `${prefix}${i}`)) i++;
  return `${prefix}${i}`;
}

function chipFromField(f: FieldDef, dateParams: string[]): Chip {
  const chip: Chip = {
    id: newChipId(),
    kind: 'field',
    name: f.name,
    args:
      f.arity === 1 ? [dateParams[0] ?? ''] :
      f.arity === 2 ? [dateParams[0] ?? '', dateParams[1] ?? dateParams[0] ?? ''] :
                      [],
  };
  //--- Per-bucket virtual entries (catalogWithBuckets) carry their bucket
  //--- key on FieldDef.default_bucket — pre-fill the chip so dropping a
  //--- "Σ cash_deposit" field produces a ready-to-go chip.
  if (f.default_bucket) chip.bucket = f.default_bucket;
  return chip;
}

export function TemplateDesignerPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [tpl, setTpl] = useState<TemplateInput>(emptyTemplate);
  const [chipsByIdx, setChipsByIdx] = useState<Chip[][]>([]);
  const [errByIdx, setErrByIdx] = useState<(string | null)[]>([]);

  const [catalog, setCatalog] = useState<FieldCatalog | null>(null);
  const [busy, setBusy] = useState(false);
  const [errs, setErrs] = useState<ValidationError[]>([]);
  const [err, setErr] = useState<string | null>(null);
  const [dpRaw, setDpRaw] = useState('date_from, date_to');

  const [dragLabel, setDragLabel] = useState<string | null>(null);
  //--- Per-column "Filter rows" panel open state (column index → open).
  const [rowFilterOpen, setRowFilterOpen] = useState<Record<number, boolean>>({});
  const sensors = useSensors(useSensor(PointerSensor, { activationConstraint: { distance: 4 } }));

  useEffect(() => {
    //--- Catalog + DepositFilter buckets merged in a single shot so K-category
    //--- entries surface per bucket key (Σ cash_deposit, # promotion, etc.)
    //--- instead of the three generic stubs that need manual bucket typing.
    loadCatalogWithBuckets().then(setCatalog).catch(e => setErr(e.message ?? 'failed to load fields'));
  }, []);

  useEffect(() => {
    if (!editing) return;
    TemplatesAPI.get(Number(id)).then((t: Template) => {
      //--- Backward-compat: if no column has pivot_key explicitly true, the
      //--- legacy "first identifier is the pivot" rule applies. Mirrors the
      //--- backend's ColumnFromJson fallback in Expression.cpp.
      const anyExplicitPivot = t.columns.some(c => c.pivot_key === true);
      const cols = anyExplicitPivot ? t.columns : t.columns.map((c, i) => {
        if (c.kind !== 'identifier') return c;
        const firstIdent = t.columns.findIndex(cc => cc.kind === 'identifier');
        return { ...c, pivot_key: i === firstIdent };
      });
      setTpl({
        name: t.name, description: t.description, row_model: t.row_model,
        date_params: t.date_params, columns: cols, sort: t.sort,
        default_top_n: t.default_top_n,
      });
      setDpRaw(t.date_params.join(', '));
      //--- derive chips from loaded AST for each formula column
      setChipsByIdx(t.columns.map(c => c.kind === 'formula' ? astToChips(c.expr ?? null) : []));
      setErrByIdx(t.columns.map(() => null));
      setReconciled(false);
    });
  }, [editing, id]);

  //--- Silent identifier-format migration. Runs once per template load,
  //--- after both the template and the catalog are available. Catches old
  //--- templates that have e.g. login (return_type=int) saved as 'money'
  //--- — the format is silently realigned in memory; the next save persists
  //--- it. Formula columns are untouched (handled separately in applyChips).
  const [reconciled, setReconciled] = useState(false);
  useEffect(() => {
    if (!catalog || reconciled || tpl.columns.length === 0) return;
    const fixed = reconcileIdentifierFormats(tpl.columns, catalog);
    if (fixed !== tpl.columns) {
      setTpl(prev => ({ ...prev, columns: fixed }));
    }
    setReconciled(true);
  }, [catalog, tpl.columns, reconciled]);

  const dateParams = useMemo(
    () => dpRaw.split(',').map(s => s.trim()).filter(Boolean),
    [dpRaw],
  );

  if (!catalog) return <div className="text-sm text-ink-400">Loading field catalog…</div>;

  const update = <K extends keyof TemplateInput>(k: K, v: TemplateInput[K]) =>
    setTpl(prev => ({ ...prev, [k]: v }));

  const updateColumn = (idx: number, patch: Partial<Column>) =>
    setTpl(prev => ({
      ...prev,
      columns: prev.columns.map((c, i) => i === idx ? { ...c, ...patch } : c),
    }));

  //--- Update a formula column's expression AND re-derive its format from
  //--- the new operands' return_types, unless the user has manually overridden
  //--- the format (heuristic: current format differs from what we'd have
  //--- auto-derived for the OLD expression). Atomic via setTpl(prev => ...)
  //--- so it composes cleanly with other patches.
  const applyExprPatch = (idx: number, ast: ExprNode | null | undefined) => {
    setTpl(prev => ({
      ...prev,
      columns: prev.columns.map((c, i) => {
        if (i !== idx) return c;
        if (!catalog || c.kind !== 'formula') {
          return { ...c, expr: ast ?? undefined };
        }
        const prevInferred = inferFormulaFormat(c.expr ?? null, catalog);
        //--- 'number' is the initial default for new formulas; treat it as
        //--- "still auto" until the user clicks the format dropdown.
        const wasAuto = c.format === 'number' || (prevInferred != null && c.format === prevInferred);
        const wantFmt = inferFormulaFormat(ast ?? null, catalog);
        const fmtPatch = (wasAuto && wantFmt && wantFmt !== c.format) ? { format: wantFmt } : {};
        return { ...c, expr: ast ?? undefined, ...fmtPatch };
      }),
    }));
  };

  //--- Core helper: mutate chips for one column, re-parse to AST.
  const applyChips = (idx: number, next: Chip[]) => {
    setChipsByIdx(prev => {
      const arr = prev.slice();
      arr[idx] = next;
      return arr;
    });
    if (next.length === 0) {
      setErrByIdx(prev => { const a = prev.slice(); a[idx] = null; return a; });
      applyExprPatch(idx, undefined);
      return;
    }
    try {
      const ast = chipsToAst(next);
      setErrByIdx(prev => { const a = prev.slice(); a[idx] = null; return a; });
      applyExprPatch(idx, ast);
    } catch (e: any) {
      setErrByIdx(prev => { const a = prev.slice(); a[idx] = e.message ?? 'parse error'; return a; });
      applyExprPatch(idx, undefined);
    }
  };

  //--- Code-view Apply: take a parsed AST, re-derive chips and replace.
  const replaceExpr = (idx: number, ast: ExprNode | null) => {
    const next = ast ? astToChips(ast) : [];
    setChipsByIdx(prev => { const a = prev.slice(); a[idx] = next; return a; });
    setErrByIdx (prev => { const a = prev.slice(); a[idx] = null; return a; });
    applyExprPatch(idx, ast);
  };

  const addIdentifierColumn = () => {
    setTpl(prev => {
      //--- First identifier in the template defaults to pivot_key=true so the
      //--- single-pivot case stays the no-config path. Subsequent identifiers
      //--- default to display-only; the user opts in by toggling.
      const hasPivot = prev.columns.some(c => c.kind === 'identifier' && c.pivot_key === true);
      return {
        ...prev,
        columns: [...prev.columns, {
          key: uniqueKey('col_', prev.columns),
          label: 'New column',
          kind: 'identifier',
          //--- Default source = login (return_type=int), so default format
          //--- is `int`. Stays consistent if the user keeps login; if they
          //--- pick a different source via FieldPicker, the format auto-
          //--- aligns via formatForReturnType.
          format: 'int',
          source: 'login',
          pivot_key: !hasPivot,
        }],
      };
    });
    setChipsByIdx(prev => [...prev, []]);
    setErrByIdx (prev => [...prev, null]);
  };

  const addFormulaColumn = () => {
    setTpl(prev => ({
      ...prev,
      columns: [...prev.columns, {
        key: uniqueKey('col_', prev.columns),
        label: 'New formula',
        kind: 'formula',
        //--- Default to `number` (decimals, no $ sign) — formulas typically
        //--- yield abstract figures (lots, ratios, net exposure) where the
        //--- currency prefix is noise. User can switch to money/int/pct
        //--- explicitly when the formula returns a currency amount or a
        //--- ratio.
        format: 'number',
      }],
    }));
    setChipsByIdx(prev => [...prev, []]);
    setErrByIdx (prev => [...prev, null]);
  };

  const removeColumn = (idx: number) => {
    setTpl(prev => ({ ...prev, columns: prev.columns.filter((_, i) => i !== idx) }));
    setChipsByIdx(prev => prev.filter((_, i) => i !== idx));
    setErrByIdx (prev => prev.filter((_, i) => i !== idx));
  };

  const moveColumn = (idx: number, dir: -1 | 1) => {
    const j = idx + dir;
    setTpl(prev => {
      if (j < 0 || j >= prev.columns.length) return prev;
      const cols = prev.columns.slice();
      [cols[idx], cols[j]] = [cols[j], cols[idx]];
      return { ...prev, columns: cols };
    });
    setChipsByIdx(prev => {
      if (j < 0 || j >= prev.length) return prev;
      const a = prev.slice();
      [a[idx], a[j]] = [a[j], a[idx]];
      return a;
    });
    setErrByIdx(prev => {
      if (j < 0 || j >= prev.length) return prev;
      const a = prev.slice();
      [a[idx], a[j]] = [a[j], a[idx]];
      return a;
    });
  };

  //--- Drag-drop into formula slots ---------------------------------

  const onDragStart = (e: DragStartEvent) => {
    const d = e.active.data.current as any;
    if (d?.kind === 'field') setDragLabel(d.field.name);
    else setDragLabel(null);
  };

  const onDragEnd = (e: DragEndEvent) => {
    setDragLabel(null);
    if (!e.over) return;
    const m = String(e.over.id).match(/^col:(\d+):slot:(\d+)$/);
    if (!m) return;
    const colIdx = Number(m[1]);
    const slotIdx = Number(m[2]);
    const data = e.active.data.current as any;
    if (data?.kind !== 'field') return;
    const f: FieldDef = data.field;
    const newChip = chipFromField(f, dateParams);
    const cur = chipsByIdx[colIdx] ?? [];
    const next = cur.slice();
    next.splice(slotIdx, 0, newChip);
    applyChips(colIdx, next);
  };

  const save = async () => {
    setBusy(true); setErr(null); setErrs([]);
    if (errByIdx.some(e => e)) {
      setErr('Some formula columns have parse errors — fix them before saving.');
      setBusy(false);
      return;
    }
    const payload: TemplateInput = { ...tpl, date_params: dateParams };
    try {
      const v = await TemplatesAPI.validate(payload);
      if (!v.ok) { setErrs(v.errors); setBusy(false); return; }

      if (editing) await TemplatesAPI.update(Number(id), payload);
      else         await TemplatesAPI.create(payload);
      nav('/templates');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <DndContext sensors={sensors} onDragStart={onDragStart} onDragEnd={onDragEnd}>
      <div className="space-y-6">
        <div className="flex items-start justify-between">
          <div>
            <Breadcrumbs items={[
              { label: 'Templates', to: '/templates' },
              { label: editing ? 'Edit template' : 'New template' },
            ]} />
            <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit template' : 'New template'}</h1>
          </div>
          <div className="flex gap-2">
            <button className="btn-secondary" onClick={() => nav('/templates')}>Cancel</button>
            <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
          </div>
        </div>

        {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}
        {errs.length > 0 && (
          <div className="card p-4 border-amber-200 bg-amber-50">
            <div className="text-sm font-semibold text-amber-900 mb-2">Validation errors:</div>
            <ul className="text-xs font-mono text-amber-800 list-disc pl-5">
              {errs.map((e, i) => <li key={i}><code>{e.path}</code>: {e.message}</li>)}
            </ul>
          </div>
        )}

        <div className="card p-5 space-y-4">
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Template</div>
          <div className="grid grid-cols-2 gap-3">
            <div>
              <label className="label">Name</label>
              <input className="input" value={tpl.name} onChange={e => update('name', e.target.value)} placeholder="Top Winner — Net Trading" />
            </div>
            <div>
              <label className="label">Default Top N</label>
              <input className="input" type="number" value={tpl.default_top_n}
                     onChange={e => update('default_top_n', Math.max(0, Number(e.target.value || 0)))} />
            </div>
          </div>
          <div>
            <label className="label">Description</label>
            <input className="input" value={tpl.description} onChange={e => update('description', e.target.value)} />
          </div>
          <div>
            <label className="label">Date params (named slots, comma-separated)</label>
            <input className="input font-mono text-xs" value={dpRaw} onChange={e => setDpRaw(e.target.value)} placeholder="date_from, date_to" />
            <div className="text-xs text-ink-500 mt-1">Formulas reference these by name. Values are provided at run time.</div>
          </div>
        </div>

        <div className="card p-5 space-y-4">
          <div className="flex items-center justify-between">
            <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Columns</div>
            <div className="flex gap-2">
              <button className="btn-secondary text-xs" onClick={addIdentifierColumn}>+ Identifier</button>
              <button className="btn-secondary text-xs" onClick={addFormulaColumn}>+ Formula</button>
            </div>
          </div>

          {tpl.columns.length === 0 && (
            <div className="text-sm text-ink-500 text-center py-8">No columns yet. Add identifier (login/group) and formula columns.</div>
          )}

          <ul className="space-y-3">
            {tpl.columns.map((c, idx) => {
              const isPivot   = c.kind === 'identifier' && c.pivot_key === true;
              //--- Rank among pivot-keyed columns ("Index 1", "Index 2", …).
              const pivotRank = isPivot
                ? tpl.columns.slice(0, idx + 1).filter(cc => cc.kind === 'identifier' && cc.pivot_key === true).length
                : 0;
              const predCount = countPredicate(c.row_predicate);
              const predSrc   = predicateSourceFor(c.source);
              //--- Quiet ink accent (matches the rest of the app palette):
              //--- a 2px darker left edge + a small grey label. No coloured
              //--- banner; no overflow clipping for inner dropdowns.
              const liClass = isPivot
                ? 'border border-ink-200 border-l-2 border-l-ink-700 rounded p-3 bg-ink-100/50'
                : 'border border-ink-200 rounded p-3 bg-ink-50/40';
              return (
              <li key={idx} className={liClass}>
                {isPivot && (
                  <div className="text-[10px] uppercase tracking-wide text-ink-500 mb-2 flex items-center gap-1.5">
                    <span className="font-medium">Index {pivotRank}</span>
                    <span className="text-ink-300">·</span>
                    <span className="font-mono font-semibold text-ink-800">{c.source || '—'}</span>
                    <span className="ml-auto opacity-50 normal-case">rows grouped by this column</span>
                  </div>
                )}
                {c.kind === 'identifier' && !isPivot && (
                  <div className="text-[10px] uppercase tracking-wide text-ink-400 mb-2">
                    Display column
                  </div>
                )}
                <div className="flex items-center gap-3 mb-3">
                  <input className="input text-sm" style={{ width: 120 }}
                         value={c.key} onChange={e => updateColumn(idx, { key: e.target.value })} placeholder="key" />
                  <input className="input text-sm flex-1" value={c.label}
                         onChange={e => updateColumn(idx, { label: e.target.value })} placeholder="Display label" />
                  <select className="input text-sm" style={{ width: 110 }}
                          value={c.kind}
                          onChange={e => updateColumn(idx, { kind: e.target.value as ColumnKind })}>
                    <option value="identifier">identifier</option>
                    <option value="formula">formula</option>
                  </select>
                  <div className="flex items-center gap-1">
                    <select className="input text-sm" style={{ width: 100 }}
                            value={c.format}
                            onChange={e => updateColumn(idx, { format: e.target.value as ColumnFormat })}>
                      <option value="money">money</option>
                      <option value="number">number</option>
                      <option value="pct">pct</option>
                      <option value="int">int</option>
                      <option value="text">text</option>
                      <option value="date">date</option>
                    </select>
                    {(() => {
                      const want = suggestedFormat(c, catalog);
                      if (!want || want === c.format) return null;
                      return (
                        <button
                          type="button"
                          title={`Auto-detect from ${c.kind === 'identifier' ? `source "${c.source}"` : 'operands'} → ${want}`}
                          className="text-xs text-blue-600 hover:text-blue-800 hover:underline px-1 whitespace-nowrap"
                          onClick={() => updateColumn(idx, { format: want })}
                        >
                          ↻ {want}
                        </button>
                      );
                    })()}
                  </div>
                  <button className="btn-secondary text-xs" onClick={() => moveColumn(idx, -1)} disabled={idx === 0}>↑</button>
                  <button className="btn-secondary text-xs" onClick={() => moveColumn(idx, +1)} disabled={idx === tpl.columns.length - 1}>↓</button>
                  {c.kind === 'formula' && (
                    <SaveAsBlueprint
                      expr={c.expr ?? null}
                      defaultName={c.label}
                      defaultDateParams={dateParams}
                    />
                  )}
                  <button className="btn-secondary text-xs text-red-600" onClick={() => removeColumn(idx)}>×</button>
                </div>

                {c.kind === 'identifier' && (
                  <div className="space-y-2">
                    <div className="flex items-center gap-2 flex-wrap">
                      <span className="text-xs text-ink-500">Source field:</span>
                      <code className="font-mono text-xs text-ink-700">{c.source || '—'}</code>
                      <FieldPicker
                        catalog={catalog}
                        filter={f => f.arity === 0 && (f.is_identifier || f.source === 'user' || f.source === 'deal')}
                        placeholder="change…"
                        onPick={f => updateColumn(idx, {
                          source: f.name,
                          //--- Align column.format to the field's return_type so
                          //--- login (int) doesn't surface as "8921.00" in Excel.
                          format: formatForReturnType(f.return_type),
                        })}
                      />
                      <label className="ml-auto inline-flex items-center gap-1.5 text-xs cursor-pointer select-none"
                             title="When on, this column contributes to the row bucket key">
                        <input type="checkbox"
                               checked={isPivot}
                               onChange={e => updateColumn(idx, { pivot_key: e.target.checked })} />
                        <span className="font-medium text-ink-700">Pivot</span>
                      </label>
                      {isPivot && (
                        <button type="button"
                                className={
                                  'text-xs px-2 py-1 rounded border font-medium ' +
                                  (predCount > 0
                                    ? 'bg-ink-100 border-ink-400 text-ink-900 hover:bg-ink-200'
                                    : 'bg-white border-ink-300 text-ink-700 hover:bg-ink-50')
                                }
                                title={`Filter which ${c.source || 'rows'} appear (applied before aggregation)`}
                                onClick={() => setRowFilterOpen(o => ({ ...o, [idx]: !o[idx] }))}>
                          {predCount > 0
                            ? `${rowFilterOpen[idx] ? '▾' : '▸'} Filter rows · ${predCount}`
                            : `${rowFilterOpen[idx] ? '▾' : '▸'} + Filter rows`}
                        </button>
                      )}
                    </div>
                    {isPivot && rowFilterOpen[idx] && catalog && (
                      <div className="border border-ink-200 bg-white rounded p-3">
                        <div className="text-[11px] text-ink-500 mb-2">
                          Only rows whose <code className="font-mono">{predSrc}</code> data matches this predicate are produced.
                        </div>
                        <PredicateEditor
                          source={predSrc}
                          filterable={catalog.filterable_by_source?.[predSrc] ?? []}
                          predicate={c.row_predicate ?? null}
                          onChange={(p) => updateColumn(idx, { row_predicate: p })}
                        />
                      </div>
                    )}
                  </div>
                )}

                {c.kind === 'formula' && (
                  <div className="space-y-2">
                    <FormulaEditor
                      chips={chipsByIdx[idx] ?? []}
                      expr={c.expr ?? null}
                      catalog={catalog}
                      dateParams={dateParams}
                      path={`col:${idx}`}
                      error={errByIdx[idx] ?? null}
                      onChipsChange={(next) => applyChips(idx, next)}
                      onExprReplace={(next) => replaceExpr(idx, next)}
                      refCandidates={tpl.columns.slice(0, idx).filter(
                        rc => rc.format !== 'text'
                      )}
                    />
                  </div>
                )}
              </li>
              );
            })}
          </ul>

          {tpl.columns.length > 0 && (
            <div className="flex gap-2 pt-2 border-t border-ink-100">
              <button className="btn-secondary text-xs" onClick={addIdentifierColumn}>+ Identifier</button>
              <button className="btn-secondary text-xs" onClick={addFormulaColumn}>+ Formula</button>
            </div>
          )}
        </div>

        <div className="card p-5 space-y-3">
          <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Sort</div>
          <div className="flex items-center gap-3">
            <select className="input" value={tpl.sort.column_key}
                    onChange={e => update('sort', { ...tpl.sort, column_key: e.target.value } as SortSpec)}>
              <option value="">— no sort —</option>
              {tpl.columns.map(c => <option key={c.key} value={c.key}>{c.label} ({c.key})</option>)}
            </select>
            <select className="input" value={tpl.sort.direction}
                    onChange={e => update('sort', { ...tpl.sort, direction: e.target.value as 'asc' | 'desc' } as SortSpec)}>
              <option value="desc">descending</option>
              <option value="asc">ascending</option>
            </select>
            <label className="inline-flex items-center gap-1.5 text-xs cursor-pointer select-none"
                   title="Sort by absolute value. Combined with direction: desc+abs = biggest magnitude on top.">
              <input type="checkbox"
                     checked={!!tpl.sort.abs}
                     onChange={e => update('sort', { ...tpl.sort, abs: e.target.checked } as SortSpec)} />
              <span className="text-ink-700">by |abs|</span>
            </label>
          </div>
        </div>
      </div>

      <DragOverlay>
        {dragLabel && (
          <div className="inline-flex items-center gap-1 px-2 py-1 border border-blue-300 bg-blue-100 text-blue-900 rounded shadow-lg font-mono text-xs">
            {dragLabel}
          </div>
        )}
      </DragOverlay>
    </DndContext>
  );
}

//--- Tiny icon-button + name-only popover for saving the current formula as
//--- a reusable blueprint. Description is left blank and date_params reuse
//--- the template's — both stay editable later via the Blueprint list page.

function SaveAsBlueprint({ expr, defaultName, defaultDateParams }: {
  expr: ExprNode | null;
  defaultName: string;
  defaultDateParams: string[];
}) {
  const [open, setOpen] = useState(false);
  const [name, setName] = useState(defaultName);
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<string | null>(null);

  const disabled = !expr;

  const save = async () => {
    if (!expr || !name.trim()) return;
    setBusy(true); setMsg(null);
    try {
      await BlueprintsAPI.create({
        name: name.trim(),
        description: '',
        date_params: defaultDateParams,
        expr,
      });
      setMsg('Saved ✓');
      setTimeout(() => { setOpen(false); setMsg(null); }, 700);
    } catch (e: any) {
      setMsg(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <span className="relative inline-flex">
      <button type="button"
              className="btn-secondary text-xs px-2 py-1 flex items-center"
              disabled={disabled}
              title={disabled ? 'Build a formula first' : 'Save as blueprint'}
              onClick={() => { setName(defaultName); setMsg(null); setOpen(o => !o); }}>
        {/* bookmark / save-for-later icon */}
        <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor"
             strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" aria-hidden>
          <path d="M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"/>
        </svg>
      </button>
      {open && (
        <div className="absolute z-30 top-7 right-0 w-64 bg-white border border-ink-200 rounded shadow-lg p-3 space-y-2"
             onMouseDown={e => e.stopPropagation()}>
          <div className="text-xs font-semibold text-ink-700">Save as blueprint</div>
          <input className="input text-xs"
                 autoFocus
                 placeholder="Name it (e.g. Net Deposits)"
                 value={name}
                 onChange={e => setName(e.target.value)}
                 onKeyDown={e => {
                   if (e.key === 'Enter') { e.preventDefault(); save(); }
                   if (e.key === 'Escape') setOpen(false);
                 }} />
          <div className="flex items-center justify-between gap-2">
            {msg && <span className={'text-[11px] ' + (msg.startsWith('Saved') ? 'text-emerald-700' : 'text-red-600 font-mono')}>{msg}</span>}
            <div className="flex gap-2 ml-auto">
              <button type="button" className="btn-secondary text-xs px-2 py-1" onClick={() => setOpen(false)}>Cancel</button>
              <button type="button" className="btn-primary text-xs px-2 py-1" disabled={busy || !name.trim()} onClick={save}>
                {busy ? 'Saving…' : 'Save'}
              </button>
            </div>
          </div>
        </div>
      )}
    </span>
  );
}
