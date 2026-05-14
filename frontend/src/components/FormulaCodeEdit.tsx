import { useEffect, useState } from 'react';
import type { ExprNode } from '../types';
import { astToText, textToAst } from '../lib/exprChips';
import { ParseError } from '../lib/exprParser';

type Props = {
  ast: ExprNode | null;
  onApply: (next: ExprNode | null) => void;
};

//--- Plain-text formula editor. Mounted/unmounted by the parent toggle, so
//--- the `ast` prop is used only as the initial text. On Apply, we parse and
//--- bubble up an AST (or null on empty input).
export function FormulaCodeEdit({ ast, onApply }: Props) {
  const [text, setText] = useState(ast ? astToText(ast) : '');
  const [err, setErr] = useState<string | null>(null);
  const [dirty, setDirty] = useState(false);

  useEffect(() => {
    setText(ast ? astToText(ast) : '');
    setDirty(false);
    setErr(null);
  }, [ast]);

  const apply = () => {
    const t = text.trim();
    if (!t) { onApply(null); setDirty(false); setErr(null); return; }
    try {
      const next = textToAst(t);
      setErr(null);
      setDirty(false);
      onApply(next);
    } catch (e: any) {
      if (e instanceof ParseError) setErr(`${e.message}`);
      else setErr(e.message ?? 'parse error');
    }
  };

  return (
    <div className="space-y-2">
      <textarea
        className="input w-full font-mono text-xs"
        rows={3}
        value={text}
        onChange={e => { setText(e.target.value); setDirty(true); setErr(null); }}
        placeholder="equity_end(date_to) - equity_start(date_from) - (sum_deposit(date_from, date_to) + sum_withdrawal(date_from, date_to))"
      />
      <div className="flex items-center gap-2">
        <button type="button" className="btn-primary text-xs px-3 py-1"
                onClick={apply} disabled={!dirty}>Apply</button>
        {err && <span className="text-xs text-red-600 font-mono">{err}</span>}
        {!err && dirty && <span className="text-xs text-amber-600">unsaved — click Apply</span>}
        {!err && !dirty && <span className="text-xs text-ink-400">in sync</span>}
      </div>
      <div className="text-[10px] text-ink-400">
        Operators <code>+ - * /</code> with standard precedence. Field calls like
        <code> name(date_from, date_to)</code>. Parentheses for grouping.
      </div>
    </div>
  );
}
