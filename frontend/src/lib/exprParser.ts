//+------------------------------------------------------------------+
//| exprParser.ts - text ↔ ExprNode (AST)                            |
//|                                                                  |
//| Grammar (left-associative, standard precedence):                 |
//|   expr        := term (('+'|'-') term)*                          |
//|   term        := factor (('*'|'/') factor)*                      |
//|   factor      := number | field_call | '(' expr ')'              |
//|   field_call  := IDENT ('(' [IDENT (',' IDENT)*] ')')? filter?   |
//|   filter      := '[' predicate ']'                               |
//|   predicate   := or_pred                                         |
//|   or_pred     := and_pred ('OR'  and_pred)*                      |
//|   and_pred    := not_pred ('AND' not_pred)*                      |
//|   not_pred    := 'NOT' not_pred | cmp | '(' predicate ')'        |
//|   cmp         := IDENT FILTER_OP value                           |
//|   FILTER_OP   := '=' | '!=' | '<' | '<=' | '>' | '>=' | '~'      |
//|                | 'contains' | 'startswith' | 'endswith' | 'in'   |
//|   value       := number | string | '[' value (',' value)* ']'    |
//+------------------------------------------------------------------+
import type { ExprNode, Predicate, FilterOp } from '../types';

export class ParseError extends Error {
  constructor(message: string, public pos: number) {
    super(message);
  }
}

type Token =
  | { kind: 'num';    value: number; pos: number }
  | { kind: 'str';    value: string; pos: number }
  | { kind: 'ident';  value: string; pos: number }
  | { kind: 'colref'; value: string; pos: number }
  | { kind: 'op';     value: '+' | '-' | '*' | '/'; pos: number }
  | { kind: 'cmp';    value: '=' | '!=' | '<' | '<=' | '>' | '>=' | '~'; pos: number }
  | { kind: 'lparen'; pos: number }
  | { kind: 'rparen'; pos: number }
  | { kind: 'lbrack'; pos: number }
  | { kind: 'rbrack'; pos: number }
  | { kind: 'comma';  pos: number };

function tokenize(s: string): Token[] {
  const out: Token[] = [];
  let i = 0;
  while (i < s.length) {
    const c = s[i];
    if (/\s/.test(c)) { i++; continue; }
    const pos = i;

    if (c === '(') { out.push({ kind: 'lparen', pos }); i++; continue; }
    if (c === ')') { out.push({ kind: 'rparen', pos }); i++; continue; }
    if (c === '[') { out.push({ kind: 'lbrack', pos }); i++; continue; }
    if (c === ']') { out.push({ kind: 'rbrack', pos }); i++; continue; }
    if (c === ',') { out.push({ kind: 'comma',  pos }); i++; continue; }

    //--- string literal "..." (supports backslash escapes for " and \)
    if (c === '"') {
      let j = i + 1;
      let buf = '';
      while (j < s.length && s[j] !== '"') {
        if (s[j] === '\\' && j + 1 < s.length) { buf += s[j + 1]; j += 2; continue; }
        buf += s[j]; j++;
      }
      if (j >= s.length) throw new ParseError(`unterminated string at ${pos}`, pos);
      out.push({ kind: 'str', value: buf, pos });
      i = j + 1;
      continue;
    }

    //--- comparison ops: != <= >= = < > ~
    if (c === '!' && s[i + 1] === '=') { out.push({ kind: 'cmp', value: '!=', pos }); i += 2; continue; }
    if (c === '<' && s[i + 1] === '=') { out.push({ kind: 'cmp', value: '<=', pos }); i += 2; continue; }
    if (c === '>' && s[i + 1] === '=') { out.push({ kind: 'cmp', value: '>=', pos }); i += 2; continue; }
    if (c === '=')                      { out.push({ kind: 'cmp', value: '=',  pos }); i++;    continue; }
    if (c === '<')                      { out.push({ kind: 'cmp', value: '<',  pos }); i++;    continue; }
    if (c === '>')                      { out.push({ kind: 'cmp', value: '>',  pos }); i++;    continue; }
    if (c === '~')                      { out.push({ kind: 'cmp', value: '~',  pos }); i++;    continue; }

    //--- arithmetic ops (also unicode variants)
    if (c === '+' || c === '*' || c === '/' || c === '-' ||
        c === '−' || c === '×' || c === '÷') {
      const op: '+' | '-' | '*' | '/' =
        c === '+' ? '+' :
        c === '*' || c === '×' ? '*' :
        c === '/' || c === '÷' ? '/' : '-';
      out.push({ kind: 'op', value: op, pos });
      i++;
      continue;
    }

    if (/[0-9.]/.test(c)) {
      let j = i;
      while (j < s.length && /[0-9.]/.test(s[j])) j++;
      const v = Number(s.slice(i, j));
      if (!Number.isFinite(v)) throw new ParseError(`invalid number at position ${pos}`, pos);
      out.push({ kind: 'num', value: v, pos });
      i = j;
      continue;
    }
    //--- @col_key column reference (must precede ident handling)
    if (c === '@') {
      let j = i + 1;
      while (j < s.length && /[a-zA-Z0-9_]/.test(s[j])) j++;
      if (j === i + 1) throw new ParseError(`empty column reference at ${pos}`, pos);
      out.push({ kind: 'colref', value: s.slice(i + 1, j), pos });
      i = j;
      continue;
    }
    if (/[a-zA-Z_]/.test(c)) {
      let j = i;
      while (j < s.length && /[a-zA-Z0-9_]/.test(s[j])) j++;
      out.push({ kind: 'ident', value: s.slice(i, j), pos });
      i = j;
      continue;
    }
    throw new ParseError(`unexpected character '${c}' at position ${pos}`, pos);
  }
  return out;
}

const FILTER_KEYWORDS_OP = ['contains', 'startswith', 'endswith', 'in', 'glob'] as const;
const PRED_KEYWORDS_BOOL = ['AND', 'OR', 'NOT'] as const;

function isFilterOpKeyword(s: string): boolean {
  return (FILTER_KEYWORDS_OP as readonly string[]).includes(s.toLowerCase());
}

function cmpTokenToOp(t: '=' | '!=' | '<' | '<=' | '>' | '>=' | '~'): FilterOp {
  switch (t) {
    case '=':  return 'eq';
    case '!=': return 'neq';
    case '<':  return 'lt';
    case '<=': return 'lte';
    case '>':  return 'gt';
    case '>=': return 'gte';
    case '~':  return 'regex';
  }
}

class Parser {
  private p = 0;
  constructor(private tokens: Token[]) {}

  peek(): Token | undefined { return this.tokens[this.p]; }
  consume(): Token { return this.tokens[this.p++]; }
  expect(kind: Token['kind']): Token {
    const t = this.peek();
    if (!t || t.kind !== kind) {
      throw new ParseError(`expected ${kind}, got ${t ? t.kind : 'end of input'}`, t?.pos ?? -1);
    }
    return this.consume();
  }
  done(): boolean { return this.p >= this.tokens.length; }
  peekPos(): number { return this.peek()?.pos ?? -1; }

  parseExpr(): ExprNode {
    let left = this.parseTerm();
    let t = this.peek();
    while (t && t.kind === 'op' && (t.value === '+' || t.value === '-')) {
      const op = t.value;
      this.consume();
      const right = this.parseTerm();
      left = { type: 'binop', op, left, right };
      t = this.peek();
    }
    return left;
  }

  parseTerm(): ExprNode {
    let left = this.parseFactor();
    let t = this.peek();
    while (t && t.kind === 'op' && (t.value === '*' || t.value === '/')) {
      const op = t.value;
      this.consume();
      const right = this.parseFactor();
      left = { type: 'binop', op, left, right };
      t = this.peek();
    }
    return left;
  }

  parseFactor(): ExprNode {
    const t = this.peek();
    if (!t) throw new ParseError('unexpected end of expression', -1);
    //--- Unary +/-: lets chips with negative literals (`-1`, `-0.5`) survive
    //--- the chip→text→AST round-trip. `-literal` folds into a single negative
    //--- literal so the AST stays compact; `-field()` becomes `0 - field()`.
    if (t.kind === 'op' && (t.value === '-' || t.value === '+')) {
      const op = t.value;
      this.consume();
      const operand = this.parseFactor();
      if (op === '+') return operand;
      if (operand.type === 'literal') return { type: 'literal', value: -operand.value };
      return { type: 'binop', op: '-',
               left: { type: 'literal', value: 0 }, right: operand };
    }
    if (t.kind === 'lparen') {
      this.consume();
      const e = this.parseExpr();
      this.expect('rparen');
      return e;
    }
    if (t.kind === 'num') {
      this.consume();
      return { type: 'literal', value: t.value };
    }
    if (t.kind === 'colref') {
      this.consume();
      return { type: 'col_ref', key: t.value };
    }
    if (t.kind === 'ident') {
      this.consume();
      const name = t.value;
      const args: string[] = [];
      if (this.peek()?.kind === 'lparen') {
        this.consume();
        if (this.peek()?.kind !== 'rparen') {
          args.push((this.expect('ident') as Extract<Token, { kind: 'ident' }>).value);
          while (this.peek()?.kind === 'comma') {
            this.consume();
            args.push((this.expect('ident') as Extract<Token, { kind: 'ident' }>).value);
          }
        }
        this.expect('rparen');
      }
      let predicate: Predicate | undefined;
      if (this.peek()?.kind === 'lbrack') {
        this.consume();
        predicate = this.parsePredicate();
        this.expect('rbrack');
      }
      const node: ExprNode = { type: 'field', name, args };
      if (predicate) (node as any).predicate = predicate;
      return node;
    }
    throw new ParseError(`unexpected token at position ${t.pos}`, t.pos);
  }

  //--- Predicate parser -----------------------------------------------

  parsePredicate(): Predicate {
    return this.parseOrPred();
  }

  private isBoolIdent(s: string, want: 'AND' | 'OR' | 'NOT'): boolean {
    return s.toUpperCase() === want;
  }

  parseOrPred(): Predicate {
    let left = this.parseAndPred();
    while (this.peek()?.kind === 'ident'
           && this.isBoolIdent((this.peek() as Extract<Token,{kind:'ident'}>).value, 'OR')) {
      this.consume();
      const right = this.parseAndPred();
      if (left.kind === 'or') left.items.push(right);
      else                    left = { kind: 'or', items: [left, right] };
    }
    return left;
  }

  parseAndPred(): Predicate {
    let left = this.parseNotPred();
    while (this.peek()?.kind === 'ident'
           && this.isBoolIdent((this.peek() as Extract<Token,{kind:'ident'}>).value, 'AND')) {
      this.consume();
      const right = this.parseNotPred();
      if (left.kind === 'and') left.items.push(right);
      else                     left = { kind: 'and', items: [left, right] };
    }
    return left;
  }

  parseNotPred(): Predicate {
    const t = this.peek();
    if (t?.kind === 'ident' && this.isBoolIdent(t.value, 'NOT')) {
      this.consume();
      return { kind: 'not', item: this.parseNotPred() };
    }
    if (t?.kind === 'lparen') {
      this.consume();
      const inner = this.parsePredicate();
      this.expect('rparen');
      return inner;
    }
    return this.parseCmpPred();
  }

  parseCmpPred(): Predicate {
    const lhs = this.expect('ident') as Extract<Token, { kind: 'ident' }>;
    let op: FilterOp;
    const next = this.peek();
    if (next?.kind === 'cmp') {
      this.consume();
      op = cmpTokenToOp(next.value);
    } else if (next?.kind === 'ident' && isFilterOpKeyword(next.value)) {
      this.consume();
      op = next.value.toLowerCase() as FilterOp;
    } else {
      throw new ParseError(`expected comparison op at ${next?.pos ?? -1}`, next?.pos ?? -1);
    }
    const value = this.parseFilterValue(op);
    return { kind: 'cmp', field: lhs.value, op, value };
  }

  parseFilterValue(op: FilterOp): number | string | string[] | number[] {
    if (op === 'in') {
      this.expect('lbrack');
      const items: (number | string)[] = [];
      if (this.peek()?.kind !== 'rbrack') {
        items.push(this.parseAtomic());
        while (this.peek()?.kind === 'comma') {
          this.consume();
          items.push(this.parseAtomic());
        }
      }
      this.expect('rbrack');
      //--- homogenize: all numbers or all strings
      const allNum = items.every(x => typeof x === 'number');
      if (allNum) return items as number[];
      return items.map(x => String(x));
    }
    return this.parseAtomic();
  }

  parseAtomic(): number | string {
    const t = this.peek();
    if (!t) throw new ParseError('unexpected end of value', -1);
    if (t.kind === 'num')   { this.consume(); return t.value; }
    if (t.kind === 'str')   { this.consume(); return t.value; }
    if (t.kind === 'ident') { this.consume(); return t.value; }  // bare ident treated as string (e.g. enum label)
    throw new ParseError(`expected number, string, or identifier at ${t.pos}`, t.pos);
  }
}

export function parseExpression(s: string): ExprNode {
  const tokens = tokenize(s);
  if (tokens.length === 0) throw new ParseError('expression is empty', 0);
  const p = new Parser(tokens);
  const expr = p.parseExpr();
  if (!p.done()) throw new ParseError(`unexpected trailing input at position ${p.peekPos()}`, p.peekPos());
  return expr;
}

//--- AST → text ---------------------------------------------------

const precedence = (op: '+' | '-' | '*' | '/') => (op === '+' || op === '-' ? 1 : 2);

export function formatExpression(node: ExprNode): string {
  return formatNode(node);
}

function formatNode(node: ExprNode): string {
  if (node.type === 'literal') return numText(node.value);
  if (node.type === 'col_ref') return `@${node.key}`;
  if (node.type === 'field') {
    const args = node.args.length === 0 ? '' : `(${node.args.join(', ')})`;
    const base = node.args.length === 0 ? node.name : `${node.name}${args}`;
    if (node.predicate) return `${base}[${formatPredicate(node.predicate)}]`;
    return base;
  }
  const myPrec = precedence(node.op);
  const left  = wrapChild(node.left,  myPrec, 'left',  node.op);
  const right = wrapChild(node.right, myPrec, 'right', node.op);
  return `${left} ${node.op} ${right}`;
}

function wrapChild(child: ExprNode, parentPrec: number, side: 'left' | 'right',
                   parentOp: '+' | '-' | '*' | '/'): string {
  const s = formatNode(child);
  if (child.type !== 'binop') return s;
  const cPrec = precedence(child.op);
  if (cPrec < parentPrec) return `(${s})`;
  if (cPrec === parentPrec && side === 'right' && (parentOp === '-' || parentOp === '/')) return `(${s})`;
  return s;
}

function numText(v: number): string { return String(v); }

//--- Predicate formatter -------------------------------------------

const FILTER_OP_TO_TEXT: Record<FilterOp, string> = {
  eq: '=', neq: '!=', lt: '<', lte: '<=', gt: '>', gte: '>=',
  regex: '~', glob: 'glob', contains: 'contains', startswith: 'startswith', endswith: 'endswith', in: 'in',
};

function quoteStr(s: string): string {
  return `"${s.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

function predRank(k: Predicate['kind']): number {
  switch (k) {
    case 'cmp': return 3;
    case 'not': return 2;
    case 'and': return 1;
    case 'or':  return 0;
  }
}

function wrapPredChild(parent: Predicate, child: Predicate): string {
  const s = formatPredicate(child);
  if (predRank(child.kind) < predRank(parent.kind)) return `(${s})`;
  return s;
}

export function formatPredicate(p: Predicate): string {
  if (p.kind === 'cmp') {
    const opTxt = FILTER_OP_TO_TEXT[p.op];
    let valTxt: string;
    if (Array.isArray(p.value)) {
      valTxt = '[' + p.value.map(v => typeof v === 'number' ? String(v) : quoteStr(String(v))).join(', ') + ']';
    } else if (typeof p.value === 'number') {
      valTxt = String(p.value);
    } else {
      valTxt = quoteStr(p.value);
    }
    return `${p.field} ${opTxt} ${valTxt}`;
  }
  if (p.kind === 'not') return `NOT ${wrapPredChild(p, p.item)}`;
  const sep = p.kind === 'and' ? ' AND ' : ' OR ';
  return p.items.map(c => wrapPredChild(p, c)).join(sep);
}
