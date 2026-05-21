import { useState } from 'react';
import {
  canonicalRangeKeys, monthRange, quarterRange, yearRange,
  daysBetween, parseYmd,
} from '../lib/dateRange';
import type { DateRange } from '../lib/dateRange';

//--- Custom date-range editor used wherever the user types in specific dates
//--- (RunReport, ReadyMade edit, scheduled-run modal). For canonical 2-param
//--- templates (date_from/date_to) it shows a "Quick fill" row (native month
//--- picker + year stepper with Q1-Q4 and Full year buttons), the two date
//--- inputs labelled "From" / "To", a live range preview, and a red-border
//--- validation when from > to. Non-canonical templates fall back to a
//--- per-param grid without the quick-fill row.

type Props = {
  dateParams: string[];
  value:      Record<string, string>;
  onChange:   (next: Record<string, string>) => void;
};

const MONTHS = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];

function formatRange(from: string, to: string): string {
  const a = parseYmd(from), b = parseYmd(to);
  if (!a || !b) return '';
  const left  = `${MONTHS[a.m - 1]} ${a.d}`;
  const right = `${MONTHS[b.m - 1]} ${b.d}`;
  return a.y === b.y
    ? `${left} – ${right}, ${a.y}`
    : `${left}, ${a.y} – ${right}, ${b.y}`;
}

export function CustomDateRangeFields({ dateParams, value, onChange }: Props) {
  const keys = canonicalRangeKeys(dateParams);

  //--- Fallback: single param or >2 params or non-from/to pair. No quick-fill.
  if (!keys) {
    return (
      <div className={`grid gap-3 ${dateParams.length > 1 ? 'grid-cols-2' : 'grid-cols-1'}`}>
        {dateParams.map(name => (
          <div key={name}>
            <label className="label">{name}</label>
            <input
              className="input"
              type="date"
              value={value[name] ?? ''}
              onChange={e => onChange({ ...value, [name]: e.target.value })}
            />
          </div>
        ))}
      </div>
    );
  }

  const [fromKey, toKey] = keys;
  const from = value[fromKey] ?? '';
  const to   = value[toKey]   ?? '';

  //--- Year stepper drives the Q1-Q4 / Full year buttons. Initial value is
  //--- derived from the current from/to (so reopening a saved range lands on
  //--- its year), falling back to broker-UTC this-year.
  const initialYear =
    parseYmd(from)?.y ?? parseYmd(to)?.y ?? new Date().getUTCFullYear();
  const [pickerYear, setPickerYear] = useState<number>(initialYear);

  //--- Native <input type="month"> wants "YYYY-MM". Derive from current from.
  const fromYmd = parseYmd(from);
  const monthVal = fromYmd
    ? `${fromYmd.y}-${String(fromYmd.m).padStart(2, '0')}`
    : '';

  const setRange = (r: DateRange) => {
    onChange({ ...value, [fromKey]: r.from, [toKey]: r.to });
    const yr = parseYmd(r.from)?.y;
    if (yr) setPickerYear(yr);
  };

  const onMonthChange = (v: string) => {
    const r = monthRange(v);
    if (r) setRange(r);
  };

  const setOne = (k: string, v: string) => onChange({ ...value, [k]: v });

  //--- Validation: both filled, both parseable, but from > to.
  const inverted = !!from && !!to && !!parseYmd(from) && !!parseYmd(to) && from > to;
  const days = daysBetween(from, to);
  const showPreview = !inverted && days > 0;

  const inputCls = (bad: boolean) =>
    'input' + (bad ? ' border-red-400 focus:border-red-500 focus:ring-red-200' : '');

  const qBtn = 'px-2.5 py-1 text-xs bg-white border border-ink-200 rounded ' +
               'hover:bg-blue-50 hover:border-blue-300 transition-colors';

  return (
    <div className="space-y-3">
      {/*--- Quick fill block ---*/}
      <div className="border border-ink-100 bg-ink-50/60 rounded-md p-3 space-y-2">
        <div className="text-[11px] uppercase tracking-wide font-medium text-ink-500">
          Quick fill
        </div>
        <div className="flex flex-wrap items-center gap-x-3 gap-y-2">
          <label className="flex items-center gap-1.5 text-xs">
            <span className="text-ink-600">Month</span>
            <input
              type="month"
              className="px-2 py-1 bg-white border border-ink-200 rounded text-sm"
              value={monthVal}
              onChange={e => onMonthChange(e.target.value)}
            />
          </label>
          <span className="text-ink-200">|</span>
          <label className="flex items-center gap-1.5 text-xs">
            <span className="text-ink-600">Year</span>
            <input
              type="number"
              className="w-20 px-2 py-1 bg-white border border-ink-200 rounded text-sm"
              min={2000}
              max={2100}
              value={pickerYear}
              onChange={e => {
                const n = parseInt(e.target.value, 10);
                if (Number.isFinite(n)) setPickerYear(n);
              }}
            />
          </label>
          <div className="flex items-center gap-1.5">
            <button type="button" className={qBtn} onClick={() => setRange(quarterRange(pickerYear, 1))}>Q1</button>
            <button type="button" className={qBtn} onClick={() => setRange(quarterRange(pickerYear, 2))}>Q2</button>
            <button type="button" className={qBtn} onClick={() => setRange(quarterRange(pickerYear, 3))}>Q3</button>
            <button type="button" className={qBtn} onClick={() => setRange(quarterRange(pickerYear, 4))}>Q4</button>
            <button type="button" className={qBtn} onClick={() => setRange(yearRange(pickerYear))}>Full year</button>
          </div>
        </div>
      </div>

      {/*--- From / To inputs ---*/}
      <div className="grid grid-cols-2 gap-3">
        <div>
          <label className="label">From</label>
          <input
            type="date"
            className={inputCls(inverted)}
            value={from}
            onChange={e => setOne(fromKey, e.target.value)}
          />
        </div>
        <div>
          <label className="label">To</label>
          <input
            type="date"
            className={inputCls(inverted)}
            value={to}
            onChange={e => setOne(toKey, e.target.value)}
          />
        </div>
      </div>

      {/*--- Preview / validation ---*/}
      {inverted && (
        <div className="text-xs text-red-600">From must be on or before To.</div>
      )}
      {showPreview && (
        <div className="text-xs text-ink-500">
          <span className="font-medium text-ink-700">{formatRange(from, to)}</span>
          {' · '}
          {days} {days === 1 ? 'day' : 'days'}
        </div>
      )}
    </div>
  );
}
