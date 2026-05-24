//+------------------------------------------------------------------+
//| exprChips.ts - AST ↔ linear chip list                            |
//+------------------------------------------------------------------+
import type { ExprNode, Predicate } from '../types';
import { parseExpression, formatExpression, formatPredicate } from './exprParser';

export type ChipOp = '+' | '-' | '*' | '/';

export type Chip =
  | { id: string; kind: 'field';     name: string; args: string[]; predicate?: Predicate; bucket?: string }
  | { id: string; kind: 'col_ref';   key:  string }
  | { id: string; kind: 'blueprint'; name: string; inner: Chip[] }
  | { id: string; kind: 'literal';   value: number }
  | { id: string; kind: 'op';        op: ChipOp }
  | { id: string; kind: 'lparen' }
  | { id: string; kind: 'rparen' };

let _seq = 0;
export const newChipId = () => `chip${++_seq}`;

const prec = (op: ChipOp) => (op === '+' || op === '-' ? 1 : 2);

//--- AST → chips (precedence-aware paren placement) ---------------

export function astToChips(node: ExprNode | null | undefined): Chip[] {
  if (!node) return [];
  return flatten(node);
}

function flatten(node: ExprNode): Chip[] {
  if (node.type === 'literal') return [{ id: newChipId(), kind: 'literal', value: node.value }];
  if (node.type === 'col_ref') return [{ id: newChipId(), kind: 'col_ref', key: node.key }];
  if (node.type === 'field') {
    const chip: Chip = { id: newChipId(), kind: 'field', name: node.name, args: [...node.args] };
    if (node.predicate) chip.predicate = node.predicate;
    if (node.bucket)    chip.bucket    = node.bucket;
    return [chip];
  }

  // binop
  const myPrec = prec(node.op);
  const leftChips  = flatten(node.left);
  const rightChips = flatten(node.right);
  const wrapLeft  = needParensLeft(node.left, myPrec);
  const wrapRight = needParensRight(node.right, myPrec, node.op);
  const out: Chip[] = [];
  if (wrapLeft)  out.push({ id: newChipId(), kind: 'lparen' });
  out.push(...leftChips);
  if (wrapLeft)  out.push({ id: newChipId(), kind: 'rparen' });
  out.push({ id: newChipId(), kind: 'op', op: node.op });
  if (wrapRight) out.push({ id: newChipId(), kind: 'lparen' });
  out.push(...rightChips);
  if (wrapRight) out.push({ id: newChipId(), kind: 'rparen' });
  return out;
}

function needParensLeft(child: ExprNode, parentPrec: number): boolean {
  if (child.type !== 'binop') return false;
  return prec(child.op) < parentPrec;
}

function needParensRight(child: ExprNode, parentPrec: number, parentOp: ChipOp): boolean {
  if (child.type !== 'binop') return false;
  const cPrec = prec(child.op);
  if (cPrec < parentPrec) return true;
  if (cPrec === parentPrec && (parentOp === '-' || parentOp === '/')) return true;
  return false;
}

//--- chips → text → AST ------------------------------------------

export function chipsToText(chips: Chip[]): string {
  return chips.map(chipText).join(' ');
}

function chipText(c: Chip): string {
  switch (c.kind) {
    case 'literal':   return String(c.value);
    case 'col_ref':   return `@${c.key}`;
    case 'blueprint': return `(${chipsToText(c.inner)})`;   // wrapper drops on save/round-trip
    case 'field': {
      const base = c.args.length ? `${c.name}(${c.args.join(', ')})` : c.name;
      return c.predicate ? `${base}[${formatPredicate(c.predicate)}]` : base;
    }
    case 'op':      return c.op;
    case 'lparen':  return '(';
    case 'rparen':  return ')';
  }
}

export function chipsToAst(chips: Chip[]): ExprNode {
  const ast = parseExpression(chipsToText(chips));
  //--- Text round-trip loses ExprField.bucket (the parser doesn't have a
  //--- text syntax for it). Walk the AST and the chip list in lock-step
  //--- left-to-right to graft bucket back onto the matching field nodes.
  const fieldChips = chips.filter(c => c.kind === 'field') as Extract<Chip, { kind: 'field' }>[];
  if (fieldChips.some(c => c.bucket)) {
    let idx = 0;
    const visit = (n: ExprNode) => {
      if (n.type === 'field') {
        if (idx < fieldChips.length && fieldChips[idx].bucket) {
          (n as { bucket?: string }).bucket = fieldChips[idx].bucket;
        }
        idx++;
      } else if (n.type === 'binop') {
        visit(n.left); visit(n.right);
      }
    };
    visit(ast);
  }
  return ast;
}

//--- AST → text (re-export for convenience) -----------------------
export { formatExpression as astToText, parseExpression as textToAst } from './exprParser';
