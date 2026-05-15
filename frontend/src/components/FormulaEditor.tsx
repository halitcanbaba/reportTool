import { useState } from 'react';
import type { Column, ExprNode, FieldCatalog } from '../types';
import type { Chip } from '../lib/exprChips';
import { FormulaBar } from './FormulaBar';
import { FormulaCodeEdit } from './FormulaCodeEdit';

type Props = {
  chips: Chip[];
  expr: ExprNode | null;
  catalog: FieldCatalog;
  dateParams: string[];
  path: string;                                 // base drop-zone id, e.g. "col:0"
  error: string | null;
  onChipsChange: (next: Chip[]) => void;
  onExprReplace: (next: ExprNode | null) => void;  // code view "Apply"
  //--- Previously-defined columns in this template, available for backward
  //--- @col_key refs. Caller passes only columns *before* this one.
  refCandidates?: Column[];
};

export function FormulaEditor({
  chips, expr, catalog, dateParams, path,
  error, onChipsChange, onExprReplace, refCandidates,
}: Props) {
  const [view, setView] = useState<'chips' | 'code'>('chips');

  return (
    <div>
      <div className="flex items-center gap-1 mb-2">
        <button type="button"
                className={
                  'text-xs px-2 py-1 rounded ' +
                  (view === 'chips' ? 'bg-ink-900 text-white' : 'bg-ink-100 text-ink-600 hover:bg-ink-200')
                }
                onClick={() => setView('chips')}>Visual</button>
        <button type="button"
                className={
                  'text-xs px-2 py-1 rounded ' +
                  (view === 'code' ? 'bg-ink-900 text-white' : 'bg-ink-100 text-ink-600 hover:bg-ink-200')
                }
                onClick={() => setView('code')}>Code</button>
        {error && view === 'chips' && (
          <span className="ml-2 text-xs text-red-600 font-mono">{error}</span>
        )}
      </div>

      {view === 'chips' ? (
        <FormulaBar
          chips={chips}
          onChipsChange={onChipsChange}
          catalog={catalog}
          dateParams={dateParams}
          path={path}
          refCandidates={refCandidates ?? []}
        />
      ) : (
        <FormulaCodeEdit ast={expr} onApply={onExprReplace} />
      )}
    </div>
  );
}
