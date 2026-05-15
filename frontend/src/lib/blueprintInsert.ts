//+------------------------------------------------------------------+
//| blueprintInsert.ts - clone a blueprint AST into a template by   |
//| renaming its date-param references.                              |
//+------------------------------------------------------------------+
import type { ExprNode } from '../types';

export type DateParamMap = Record<string, string>;   // blueprint param → template param

//--- Deep clone + remap. Predicate trees inside Field chips are preserved as-is
//--- (predicate values reference row fields, not template date params).
export function remapExpr(node: ExprNode, mapping: DateParamMap): ExprNode {
  if (node.type === 'literal') return { ...node };
  if (node.type === 'col_ref') return { ...node };
  if (node.type === 'field') {
    return {
      ...node,
      args: node.args.map(a => mapping[a] ?? a),
      predicate: node.predicate ? structuredClone(node.predicate) : undefined,
    };
  }
  return {
    type: 'binop',
    op: node.op,
    left:  remapExpr(node.left,  mapping),
    right: remapExpr(node.right, mapping),
  };
}

//--- Build an identity mapping where blueprint params that already exist in
//--- the template's date_params keep their names; unmapped params remain in
//--- the result map as empty strings (caller must let the user fill them).
export function autoMatch(blueprintParams: string[], templateParams: string[]): DateParamMap {
  const tset = new Set(templateParams);
  const out: DateParamMap = {};
  for (const p of blueprintParams) out[p] = tset.has(p) ? p : '';
  return out;
}

export function isMappingComplete(map: DateParamMap): boolean {
  return Object.values(map).every(v => v.length > 0);
}

//--- Walk a Predicate and collect distinct cmp.field names (first-seen order).
export function collectPredicateFields(p: import('../types').Predicate | null | undefined): string[] {
  if (!p) return [];
  const out: string[] = [];
  const seen = new Set<string>();
  const walk = (node: import('../types').Predicate) => {
    if (node.kind === 'cmp') {
      if (!seen.has(node.field)) { seen.add(node.field); out.push(node.field); }
      return;
    }
    if (node.kind === 'not') { walk(node.item); return; }
    for (const c of node.items) walk(c);
  };
  walk(p);
  return out;
}
