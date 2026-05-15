import { useEffect, useMemo, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { DndContext, DragOverlay, PointerSensor, useSensor, useSensors,
         type DragEndEvent, type DragStartEvent } from '@dnd-kit/core';
import { TemplatesAPI } from '../api/templates';
import { FieldsAPI } from '../api/fields';
import type {
  Column, ColumnFormat, ColumnKind, ExprNode, FieldCatalog, FieldDef,
  SortSpec, Template, TemplateInput, ValidationError,
} from '../types';
import { FormulaEditor } from '../components/FormulaEditor';
import { FieldPicker } from '../components/FieldPicker';
import { Breadcrumbs } from '../components/Breadcrumbs';
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
  const sensors = useSensors(useSensor(PointerSensor, { activationConstraint: { distance: 4 } }));

  useEffect(() => {
    FieldsAPI.catalog().then(setCatalog).catch(e => setErr(e.message ?? 'failed to load fields'));
  }, []);

  useEffect(() => {
    if (!editing) return;
    TemplatesAPI.get(Number(id)).then((t: Template) => {
      setTpl({
        name: t.name, description: t.description, row_model: t.row_model,
        date_params: t.date_params, columns: t.columns, sort: t.sort,
        default_top_n: t.default_top_n,
      });
      setDpRaw(t.date_params.join(', '));
      //--- derive chips from loaded AST for each formula column
      setChipsByIdx(t.columns.map(c => c.kind === 'formula' ? astToChips(c.expr ?? null) : []));
      setErrByIdx(t.columns.map(() => null));
    });
  }, [editing, id]);

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

  //--- Core helper: mutate chips for one column, re-parse to AST.
  const applyChips = (idx: number, next: Chip[]) => {
    setChipsByIdx(prev => {
      const arr = prev.slice();
      arr[idx] = next;
      return arr;
    });
    if (next.length === 0) {
      setErrByIdx(prev => { const a = prev.slice(); a[idx] = null; return a; });
      updateColumn(idx, { expr: undefined });
      return;
    }
    try {
      const ast = chipsToAst(next);
      setErrByIdx(prev => { const a = prev.slice(); a[idx] = null; return a; });
      updateColumn(idx, { expr: ast });
    } catch (e: any) {
      setErrByIdx(prev => { const a = prev.slice(); a[idx] = e.message ?? 'parse error'; return a; });
      updateColumn(idx, { expr: undefined });
    }
  };

  //--- Code-view Apply: take a parsed AST, re-derive chips and replace.
  const replaceExpr = (idx: number, ast: ExprNode | null) => {
    const next = ast ? astToChips(ast) : [];
    setChipsByIdx(prev => { const a = prev.slice(); a[idx] = next; return a; });
    setErrByIdx (prev => { const a = prev.slice(); a[idx] = null; return a; });
    updateColumn(idx, { expr: ast ?? undefined });
  };

  const addIdentifierColumn = () => {
    setTpl(prev => ({
      ...prev,
      columns: [...prev.columns, {
        key: uniqueKey('col_', prev.columns),
        label: 'New column',
        kind: 'identifier',
        format: 'text',
        source: 'login',
      }],
    }));
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
        format: 'money',
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
            {tpl.columns.map((c, idx) => (
              <li key={idx} className="border border-ink-200 rounded p-3 bg-ink-50/40">
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
                  <select className="input text-sm" style={{ width: 100 }}
                          value={c.format}
                          onChange={e => updateColumn(idx, { format: e.target.value as ColumnFormat })}>
                    <option value="money">money</option>
                    <option value="pct">pct</option>
                    <option value="int">int</option>
                    <option value="text">text</option>
                    <option value="date">date</option>
                  </select>
                  <button className="btn-secondary text-xs" onClick={() => moveColumn(idx, -1)} disabled={idx === 0}>↑</button>
                  <button className="btn-secondary text-xs" onClick={() => moveColumn(idx, +1)} disabled={idx === tpl.columns.length - 1}>↓</button>
                  <button className="btn-secondary text-xs text-red-600" onClick={() => removeColumn(idx)}>×</button>
                </div>

                {c.kind === 'identifier' && (
                  <div className="flex items-center gap-2">
                    <span className="text-xs text-ink-500">Source field:</span>
                    <code className="font-mono text-xs text-ink-700">{c.source || '—'}</code>
                    <FieldPicker
                      catalog={catalog}
                      filter={f => f.arity === 0 && (f.is_identifier || f.source === 'user')}
                      placeholder="change…"
                      onPick={f => updateColumn(idx, { source: f.name })}
                    />
                  </div>
                )}

                {c.kind === 'formula' && (
                  <div className="space-y-2">
                    <div className="flex items-center gap-2">
                      <SaveAsBlueprint
                        expr={c.expr ?? null}
                        defaultName={c.label}
                        defaultDateParams={dateParams}
                      />
                    </div>
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
            ))}
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

//--- Inline "Save as blueprint" popover -----------------------------

function SaveAsBlueprint({ expr, defaultName, defaultDateParams }: {
  expr: ExprNode | null;
  defaultName: string;
  defaultDateParams: string[];
}) {
  const [open, setOpen] = useState(false);
  const [name, setName] = useState(defaultName);
  const [description, setDescription] = useState('');
  const [dpRaw, setDpRaw] = useState(defaultDateParams.join(', '));
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<string | null>(null);

  const disabled = !expr;

  const save = async () => {
    if (!expr) return;
    setBusy(true); setMsg(null);
    const params = dpRaw.split(',').map(s => s.trim()).filter(Boolean);
    try {
      await BlueprintsAPI.create({
        name: name.trim(),
        description: description.trim(),
        date_params: params,
        expr,
      });
      setMsg('Saved ✓');
      setTimeout(() => { setOpen(false); setMsg(null); }, 800);
    } catch (e: any) {
      setMsg(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <span className="relative inline-flex">
      <button type="button"
              className="btn-secondary text-xs"
              disabled={disabled}
              title={disabled ? 'Build a formula first' : 'Save this formula as a reusable blueprint'}
              onClick={() => setOpen(o => !o)}>
        Save as blueprint…
      </button>
      {open && (
        <div className="absolute z-30 top-7 left-0 w-80 bg-white border border-ink-200 rounded shadow-lg p-3 space-y-2"
             onMouseDown={e => e.stopPropagation()}>
          <div className="text-sm font-semibold">Save formula as blueprint</div>
          <div>
            <label className="label">Name</label>
            <input className="input text-xs" value={name} onChange={e => setName(e.target.value)} placeholder="Net Deposits" />
          </div>
          <div>
            <label className="label">Description (optional)</label>
            <input className="input text-xs" value={description} onChange={e => setDescription(e.target.value)} />
          </div>
          <div>
            <label className="label">Date params</label>
            <input className="input font-mono text-xs" value={dpRaw} onChange={e => setDpRaw(e.target.value)} />
            <div className="text-[10px] text-ink-500 mt-1">Names used inside the formula; used for remapping on insert.</div>
          </div>
          <div className="flex items-center justify-end gap-2">
            {msg && <span className={'text-xs mr-auto ' + (msg.startsWith('Saved') ? 'text-emerald-700' : 'text-red-600 font-mono')}>{msg}</span>}
            <button type="button" className="btn-secondary text-xs" onClick={() => setOpen(false)}>Cancel</button>
            <button type="button" className="btn-primary text-xs" disabled={busy || !name.trim()} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
          </div>
        </div>
      )}
    </span>
  );
}
