import { useMemo } from 'react';
import { PRESETS, canonicalRangeKeys, resolvePreset } from '../lib/dateRange';
import type { DatePresetKey } from '../lib/dateRange';
import type { RelativePreset, Template } from '../types';

//--- Date strategy picker for ready-made reports.
//--- Preset chip selected → date_strategy='relative' with backend-mapped preset
//--- (re-computed at every run). "Custom" → date_strategy='fixed' with the
//--- typed-in YYYY-MM-DD per template date param.

export type StrategyValue = {
  date_strategy:   'fixed' | 'relative';
  fixed_dates:     Record<string, string>;
  relative_preset: RelativePreset;
  relative_n:      number;
};

type Props = {
  template: Template | null;
  value:    StrategyValue;
  onChange: (next: StrategyValue) => void;
};

//--- UI preset chip → backend representation. The `_to_date` variants
//--- include today (partial daily snapshot); the base variants stop at
//--- yesterday so equity_end-based formulas see a sealed snapshot.
const presetMap: Record<DatePresetKey, { preset: RelativePreset; n: number }> = {
  today:              { preset: 'today',                n: 7  },
  yesterday:          { preset: 'yesterday',            n: 7  },
  last_7:             { preset: 'last_n_days',          n: 7  },
  last_7_to_date:     { preset: 'last_n_days_to_date',  n: 7  },
  last_30:            { preset: 'last_n_days',          n: 30 },
  last_30_to_date:    { preset: 'last_n_days_to_date',  n: 30 },
  this_week:          { preset: 'this_week',            n: 7  },
  this_week_to_date:  { preset: 'this_week_to_date',    n: 7  },
  last_week:          { preset: 'last_week',            n: 7  },
  this_month:         { preset: 'this_month',           n: 7  },
  this_month_to_date: { preset: 'this_month_to_date',   n: 7  },
  last_month:         { preset: 'last_month',           n: 7  },
};

//--- Reverse map (relative_preset + n → chip key). Only canonical chips
//--- here; non-matching (e.g. last_n_days with n=42) returns null so the
//--- UI falls back to "Custom" mode visually.
function detectChip(v: StrategyValue): DatePresetKey | null {
  if (v.date_strategy !== 'relative') return null;
  if (v.relative_preset === 'last_n_days') {
    if (v.relative_n === 7)  return 'last_7';
    if (v.relative_n === 30) return 'last_30';
    return null;
  }
  if (v.relative_preset === 'last_n_days_to_date') {
    if (v.relative_n === 7)  return 'last_7_to_date';
    if (v.relative_n === 30) return 'last_30_to_date';
    return null;
  }
  for (const [chip, m] of Object.entries(presetMap)) {
    if (m.preset === v.relative_preset && m.n === 7) return chip as DatePresetKey;
  }
  return null;
}

export function DateStrategyPicker({ template, value, onChange }: Props) {
  const active = useMemo(() => detectChip(value), [value]);
  const isCustom = value.date_strategy === 'fixed';

  //--- Preview text for active preset (what dates it would resolve to NOW).
  const previewRange = useMemo(() => {
    if (!active) return null;
    return resolvePreset(active);
  }, [active]);

  const applyPreset = (key: DatePresetKey) => {
    const m = presetMap[key];
    onChange({
      date_strategy:   'relative',
      fixed_dates:     {},
      relative_preset: m.preset,
      relative_n:      m.n,
    });
  };

  const switchToCustom = () => {
    //--- Seed fixed_dates with the active preset's current resolution so the
    //--- user can tweak from a sensible starting point.
    if (!template || template.date_params.length === 0) {
      onChange({ ...value, date_strategy: 'fixed' });
      return;
    }
    const seed: Record<string, string> = { ...value.fixed_dates };
    if (active && template.date_params.length >= 2) {
      const r = resolvePreset(active);
      const keys = canonicalRangeKeys(template.date_params);
      if (keys) { seed[keys[0]] = r.from; seed[keys[1]] = r.to; }
    }
    onChange({ ...value, date_strategy: 'fixed', fixed_dates: seed });
  };

  const setFixed = (name: string, v: string) =>
    onChange({ ...value, date_strategy: 'fixed', fixed_dates: { ...value.fixed_dates, [name]: v } });

  if (!template) {
    return <div className="text-xs text-ink-500">Select a template first to choose its date strategy.</div>;
  }
  if (template.date_params.length === 0) {
    return <div className="text-xs text-ink-500">Template has no date parameters — nothing to set.</div>;
  }

  return (
    <div className="space-y-3">
      <div className="flex flex-wrap gap-1.5">
        {PRESETS.map(p => (
          <button
            key={p.key}
            type="button"
            onClick={() => applyPreset(p.key)}
            className={
              'px-2.5 py-1 text-xs rounded-md border transition-colors ' +
              (active === p.key
                ? 'bg-blue-600 text-white border-blue-600'
                : 'bg-white text-ink-700 border-ink-200 hover:border-blue-400 hover:bg-blue-50')
            }
          >
            {p.label}
          </button>
        ))}
        <button
          type="button"
          onClick={switchToCustom}
          className={
            'px-2.5 py-1 text-xs rounded-md border transition-colors ' +
            (isCustom
              ? 'bg-ink-900 text-white border-ink-900'
              : 'bg-white text-ink-700 border-ink-200 hover:border-ink-400 hover:bg-ink-50')
          }
        >
          Custom dates
        </button>
      </div>

      {active && previewRange && (
        <div className="text-[11px] text-ink-500 bg-blue-50 border border-blue-100 rounded px-3 py-2">
          Re-computed at every run. Today it would resolve to{' '}
          <span className="font-mono">{previewRange.from}</span> →{' '}
          <span className="font-mono">{previewRange.to}</span> (UTC).
        </div>
      )}

      {isCustom && (
        <div className={`grid gap-3 ${template.date_params.length > 1 ? 'grid-cols-2' : 'grid-cols-1'}`}>
          {template.date_params.map(name => (
            <div key={name}>
              <label className="label font-mono">{name}</label>
              <input
                className="input"
                type="date"
                value={value.fixed_dates[name] ?? ''}
                onChange={e => setFixed(name, e.target.value)}
              />
            </div>
          ))}
        </div>
      )}

      <div className="text-[11px] text-ink-400">
        Dates interpreted in UTC (MT5 broker trading day).
      </div>
    </div>
  );
}
