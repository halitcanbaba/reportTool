import type {
  Column, ColumnFormat, ExprNode, FieldCatalog, FieldDef,
} from '../types';

//--- Maps a field's semantic return_type to the column's display format.
//--- Used when (a) the user picks a new source field for an identifier
//--- column, (b) loading a template (silent format migration), and (c)
//--- showing the "auto-detect" hint next to the format dropdown.
export function formatForReturnType(rt: FieldDef['return_type']): ColumnFormat {
  if (rt === 'int')   return 'int';
  if (rt === 'money') return 'money';
  if (rt === 'pct')   return 'pct';
  if (rt === 'date')  return 'date';
  return 'text';
}

//--- Walks a formula AST and returns the format the column SHOULD have
//--- based on its operand return_types. Returns null when there's nothing
//--- to infer (no fields referenced, e.g. literals only).
//---
//--- Rules:
//---   no field references          -> null  (caller keeps current format)
//---   any field is 'pct'           -> 'pct' (pct dominates ratios)
//---   all fields 'money', only +/- -> 'money'
//---   all fields 'int',   only +/- -> 'int'
//---   otherwise                    -> 'number'
//---
//--- * and / change the unit (e.g. pl / count = ratio), so they bump the
//--- result to 'number' which renders as decimals with no currency sign.
export function inferFormulaFormat(
  expr: ExprNode | null | undefined,
  catalog: FieldCatalog | null,
): ColumnFormat | null {
  if (!expr || !catalog) return null;

  const fieldByName = new Map(catalog.fields.map(f => [f.name, f]));
  const returnTypes = new Set<FieldDef['return_type']>();
  let hasMulDiv = false;
  let fieldCount = 0;

  const walk = (n: ExprNode): void => {
    if (n.type === 'literal' || n.type === 'col_ref') return;
    if (n.type === 'field') {
      const f = fieldByName.get(n.name);
      if (f) {
        returnTypes.add(f.return_type);
        fieldCount++;
      }
      return;
    }
    //--- binop
    if (n.op === '*' || n.op === '/') hasMulDiv = true;
    walk(n.left);
    walk(n.right);
  };
  walk(expr);

  if (fieldCount === 0) return null;
  if (returnTypes.has('pct')) return 'pct';
  if (hasMulDiv) return 'number';
  if (returnTypes.size === 1 && returnTypes.has('money')) return 'money';
  if (returnTypes.size === 1 && returnTypes.has('int'))   return 'int';
  return 'number';
}

//--- Silent migration for stored templates: when a template loads, every
//--- identifier column's format is realigned to its source field's
//--- return_type. Catches old templates where login/group/comment etc.
//--- were saved as 'money' before the auto-derive fix landed. Formula
//--- columns are NOT touched here — that lives in inferFormulaFormat and
//--- only fires when the user edits the expression.
export function reconcileIdentifierFormats(
  cols: Column[],
  catalog: FieldCatalog,
): Column[] {
  const fieldByName = new Map(catalog.fields.map(f => [f.name, f]));
  let mutated = false;
  const next = cols.map(c => {
    if (c.kind !== 'identifier' || !c.source) return c;
    const f = fieldByName.get(c.source);
    if (!f) return c;
    const want = formatForReturnType(f.return_type);
    if (c.format === want) return c;
    mutated = true;
    return { ...c, format: want };
  });
  return mutated ? next : cols;
}

//--- "What format would the system suggest for this column?" Used to render
//--- the inline "↻ auto-detect" hint next to the format dropdown. Returns
//--- null when there's no useful suggestion (formula with no fields,
//--- identifier with no source).
export function suggestedFormat(c: Column, catalog: FieldCatalog | null): ColumnFormat | null {
  if (!catalog) return null;
  if (c.kind === 'identifier') {
    if (!c.source) return null;
    const f = catalog.fields.find(x => x.name === c.source);
    return f ? formatForReturnType(f.return_type) : null;
  }
  return inferFormulaFormat(c.expr ?? null, catalog);
}
