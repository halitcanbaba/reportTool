import { useEffect, useState } from 'react';
import { BlueprintsAPI } from '../api/blueprints';
import type { FormulaBlueprint, ExprNode } from '../types';
import { astToText } from '../lib/exprChips';
import { autoMatch, isMappingComplete, remapExpr, type DateParamMap } from '../lib/blueprintInsert';

type Props = {
  templateDateParams: string[];
  onInsert: (expr: ExprNode, blueprintName: string) => void;
};

//--- Inline blueprint picker: list + optional remap modal. Always rendered,
//--- caller decides when to mount/unmount (e.g. inside an insertion popover).
export function BlueprintPicker({ templateDateParams, onInsert }: Props) {
  const [items, setItems] = useState<FormulaBlueprint[]>([]);
  const [loading, setLoading] = useState(true);
  const [err, setErr] = useState<string | null>(null);
  const [mappingFor, setMappingFor] = useState<FormulaBlueprint | null>(null);
  const [mapping, setMapping] = useState<DateParamMap>({});

  useEffect(() => {
    BlueprintsAPI.list()
      .then(rs => { setItems(rs); setErr(null); })
      .catch(e => setErr(e.message ?? 'failed to load'))
      .finally(() => setLoading(false));
  }, []);

  const pick = (b: FormulaBlueprint) => {
    const initial = autoMatch(b.date_params, templateDateParams);
    if (isMappingComplete(initial)) {
      onInsert(remapExpr(b.expr, initial), b.name);
      return;
    }
    setMapping(initial);
    setMappingFor(b);
  };

  const applyMapping = () => {
    if (!mappingFor) return;
    if (!isMappingComplete(mapping)) { setErr('map all blueprint date params'); return; }
    onInsert(remapExpr(mappingFor.expr, mapping), mappingFor.name);
    setMappingFor(null); setErr(null);
  };

  if (mappingFor) {
    return (
      <div className="border border-ink-200 rounded p-2 bg-ink-50/50">
        <div className="text-xs font-semibold mb-2">Map date params for "{mappingFor.name}"</div>
        <div className="space-y-1.5">
          {mappingFor.date_params.map(p => (
            <div key={p} className="flex items-center gap-2">
              <code className="font-mono text-[11px] text-ink-700 w-20">{p}</code>
              <span className="text-ink-400 text-xs">→</span>
              <select className="input text-xs flex-1"
                      value={mapping[p] ?? ''}
                      onChange={e => setMapping({ ...mapping, [p]: e.target.value })}>
                <option value="">— pick —</option>
                {templateDateParams.map(t => <option key={t} value={t}>{t}</option>)}
              </select>
            </div>
          ))}
        </div>
        <div className="flex items-center justify-end gap-2 mt-2">
          {err && <span className="text-xs text-red-600 mr-auto">{err}</span>}
          <button type="button" className="btn-secondary text-xs px-2 py-0.5"
                  onClick={() => { setMappingFor(null); setErr(null); }}>Cancel</button>
          <button type="button" className="btn-primary text-xs px-2 py-0.5" onClick={applyMapping}>Insert</button>
        </div>
      </div>
    );
  }

  return (
    <div className="space-y-1">
      <div className="text-[10px] uppercase font-semibold text-ink-500">Blueprints</div>
      {loading && <div className="text-xs text-ink-400">Loading…</div>}
      {err && <div className="text-xs text-red-600 font-mono">{err}</div>}
      {!loading && !err && items.length === 0 && (
        <div className="text-[11px] text-ink-400 italic">No blueprints saved yet.</div>
      )}
      <ul className="max-h-48 overflow-auto border border-ink-100 rounded bg-white">
        {items.map(b => {
          let preview = '';
          try { preview = astToText(b.expr); } catch { preview = '(unrenderable)'; }
          return (
            <li key={b.id}>
              <button type="button"
                      className="block w-full text-left px-2 py-1.5 hover:bg-ink-50 border-b border-ink-50 last:border-0"
                      onClick={() => pick(b)}>
                <div className="text-xs font-medium text-ink-900">{b.name}</div>
                {b.description && <div className="text-[10px] text-ink-500">{b.description}</div>}
                <div className="text-[10px] text-ink-700 font-mono truncate" title={preview}>{preview}</div>
              </button>
            </li>
          );
        })}
      </ul>
    </div>
  );
}
