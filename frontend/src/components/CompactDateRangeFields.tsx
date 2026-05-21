import { useMemo } from 'react';
import {
  PRESETS, canonicalRangeKeys, detectPreset, resolvePreset,
  monthRange, daysBetween, parseYmd,
} from '../lib/dateRange';
import type { DatePresetKey } from '../lib/dateRange';

//--- Compact date-range editor for the "Run with…" modal on ReadyMadeListPage.
//--- The full DateParamsForm fits poorly in a 640px modal (12 wrapping chips
//--- + Quick-fill box + From/To + preview + footer = 5+ stacked blocks). Here
//--- we collapse preset chips into a single <select>, drop the quick-fill
//--- year/quarter/full-year row, and keep just the month picker inline with
//--- the date inputs. Other pages still use DateParamsForm.

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

export function CompactDateRangeFields({ dateParams, value, onChange }: Props) {
  const keys = canonicalRangeKeys(dateParams);

  //--- Non-canonical (single or 3+ params): fall back to a per-param grid.
  //--- No preset select, no month picker — those only make sense for from/to.
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

  const activePreset = useMemo<DatePresetKey | null>(
    () => detectPreset(from, to),
    [from, to],
  );

  const applyPreset = (key: DatePresetKey | '') => {
    if (!key) return; //--- "— Custom —" keeps current dates
    const r = resolvePreset(key);
    onChange({ ...value, [fromKey]: r.from, [toKey]: r.to });
  };

  const applyMonth = (yyyymm: string) => {
    const r = monthRange(yyyymm);
    if (r) onChange({ ...value, [fromKey]: r.from, [toKey]: r.to });
  };

  const setOne = (k: string, v: string) =>
    onChange({ ...value, [k]: v });

  const fromYmd = parseYmd(from);
  const monthVal = fromYmd
    ? `${fromYmd.y}-${String(fromYmd.m).padStart(2, '0')}`
    : '';

  const inverted = !!from && !!to && !!parseYmd(from) && !!parseYmd(to) && from > to;
  const days = daysBetween(from, to);
  const showPreview = !inverted && days > 0;

  const inputCls = (bad: boolean) =>
    'input' + (bad ? ' border-red-400 focus:border-red-500 focus:ring-red-200' : '');

  return (
    <div className="space-y-2">
      <div className="flex items-center gap-2">
        <label className="text-xs text-ink-600 whitespace-nowrap">Preset</label>
        <select
          className="input text-sm flex-1"
          value={activePreset ?? ''}
          onChange={e => applyPreset(e.target.value as DatePresetKey | '')}
        >
          <option value="">— Custom —</option>
          {PRESETS.map(p => (
            <option key={p.key} value={p.key}>{p.label}</option>
          ))}
        </select>
      </div>

      <div className="flex items-center gap-2">
        <label className="text-xs text-ink-600 whitespace-nowrap">Month</label>
        <input
          type="month"
          className="px-2 py-1 bg-white border border-ink-200 rounded text-sm flex-1"
          value={monthVal}
          onChange={e => applyMonth(e.target.value)}
          title="Pick any month — fills From/To to that month's first and last day"
        />
      </div>

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
