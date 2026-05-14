import { useEffect, useMemo, useState } from 'react';
import { useNavigate, useParams } from 'react-router-dom';
import { BlueprintsAPI } from '../api/blueprints';
import { FieldsAPI } from '../api/fields';
import type { ExprNode, FieldCatalog, FormulaBlueprintInput } from '../types';
import { FormulaEditor } from '../components/FormulaEditor';
import { Breadcrumbs } from '../components/Breadcrumbs';
import { astToChips, chipsToAst, type Chip } from '../lib/exprChips';

const emptyBlueprint: FormulaBlueprintInput = {
  name: '',
  description: '',
  date_params: ['date_from', 'date_to'],
  expr: { type: 'literal', value: 0 },   // placeholder; user replaces immediately
};

export function BlueprintEditPage() {
  const { id } = useParams();
  const editing = id != null;
  const nav = useNavigate();

  const [form, setForm] = useState<FormulaBlueprintInput>(emptyBlueprint);
  const [chips, setChips] = useState<Chip[]>([]);
  const [exprErr, setExprErr] = useState<string | null>(null);
  const [catalog, setCatalog] = useState<FieldCatalog | null>(null);
  const [dpRaw, setDpRaw] = useState('date_from, date_to');
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  useEffect(() => {
    FieldsAPI.catalog().then(setCatalog).catch(e => setErr(e.message ?? 'failed to load fields'));
  }, []);

  useEffect(() => {
    if (!editing) return;
    BlueprintsAPI.get(Number(id)).then(b => {
      setForm({
        name: b.name, description: b.description,
        date_params: b.date_params, expr: b.expr,
      });
      setDpRaw(b.date_params.join(', '));
      setChips(astToChips(b.expr));
    }).catch(e => setErr(e.message ?? 'failed to load'));
  }, [editing, id]);

  const dateParams = useMemo(
    () => dpRaw.split(',').map(s => s.trim()).filter(Boolean),
    [dpRaw],
  );

  if (!catalog) return <div className="text-sm text-ink-400">Loading field catalog…</div>;

  const update = <K extends keyof FormulaBlueprintInput>(k: K, v: FormulaBlueprintInput[K]) =>
    setForm(prev => ({ ...prev, [k]: v }));

  const applyChips = (next: Chip[]) => {
    setChips(next);
    if (next.length === 0) { setExprErr(null); return; }
    try {
      const ast = chipsToAst(next);
      setExprErr(null);
      update('expr', ast);
    } catch (e: any) {
      setExprErr(e.message ?? 'parse error');
    }
  };

  const replaceExpr = (ast: ExprNode | null) => {
    if (!ast) { setChips([]); setExprErr(null); return; }
    setChips(astToChips(ast));
    setExprErr(null);
    update('expr', ast);
  };

  const save = async () => {
    if (chips.length === 0) { setErr('formula is empty'); return; }
    if (exprErr) { setErr('fix the formula parse error before saving'); return; }
    setBusy(true); setErr(null);
    const payload: FormulaBlueprintInput = { ...form, date_params: dateParams };
    try {
      if (editing) await BlueprintsAPI.update(Number(id), payload);
      else         await BlueprintsAPI.create(payload);
      nav('/blueprints');
    } catch (e: any) {
      setErr(e.message ?? 'save failed');
    } finally { setBusy(false); }
  };

  return (
    <div className="space-y-6">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'Blueprints', to: '/blueprints' },
            { label: editing ? 'Edit blueprint' : 'New blueprint' },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">{editing ? 'Edit blueprint' : 'New blueprint'}</h1>
        </div>
        <div className="flex gap-2">
          <button className="btn-secondary" onClick={() => nav('/blueprints')}>Cancel</button>
          <button className="btn-primary" disabled={busy} onClick={save}>{busy ? 'Saving…' : 'Save'}</button>
        </div>
      </div>

      {err && <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">{err}</div>}

      <div className="card p-5 space-y-4">
        <div className="grid grid-cols-2 gap-3">
          <div>
            <label className="label">Name</label>
            <input className="input" value={form.name} onChange={e => update('name', e.target.value)}
                   placeholder="Net Deposits" />
          </div>
          <div>
            <label className="label">Date param slots (named, comma-separated)</label>
            <input className="input font-mono text-xs" value={dpRaw}
                   onChange={e => setDpRaw(e.target.value)} placeholder="date_from, date_to" />
            <div className="text-xs text-ink-500 mt-1">Inside the formula, refer to these names. When inserting into a template, you can remap them.</div>
          </div>
        </div>
        <div>
          <label className="label">Description</label>
          <input className="input" value={form.description} onChange={e => update('description', e.target.value)} />
        </div>
      </div>

      <div className="card p-5 space-y-3">
        <div className="text-sm font-semibold text-ink-700 uppercase tracking-wide">Formula</div>
        <FormulaEditor
          chips={chips}
          expr={form.expr ?? null}
          catalog={catalog}
          dateParams={dateParams}
          path={`bp:${editing ? id : 'new'}`}
          error={exprErr}
          onChipsChange={applyChips}
          onExprReplace={replaceExpr}
        />
      </div>
    </div>
  );
}
