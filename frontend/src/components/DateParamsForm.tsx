import { useMemo } from 'react';
import { PRESETS, canonicalRangeKeys, detectPreset, resolvePreset } from '../lib/dateRange';
import type { DatePresetKey } from '../lib/dateRange';

//--- Unified date-parameter input.
//--- For 2-param templates: shows a row of preset chips (Today / Yesterday /
//--- Last 7 days / This week / Last week / This month / Last month / Custom)
//--- + per-param date inputs. Editing the inputs disables the active preset.
//--- For other arities: just renders the inputs.

type Props = {
  dateParams: string[];                         // template's date_params
  value:      Record<string, string>;           // current YYYY-MM-DD per param
  onChange:   (next: Record<string, string>) => void;
};

export function DateParamsForm({ dateParams, value, onChange }: Props) {
  const rangeKeys = canonicalRangeKeys(dateParams);

  const active = useMemo<DatePresetKey | null>(() => {
    if (!rangeKeys) return null;
    return detectPreset(value[rangeKeys[0]] ?? '', value[rangeKeys[1]] ?? '');
  }, [rangeKeys, value]);

  const applyPreset = (key: DatePresetKey) => {
    if (!rangeKeys) return;
    const r = resolvePreset(key);
    onChange({ ...value, [rangeKeys[0]]: r.from, [rangeKeys[1]]: r.to });
  };

  const setOne = (name: string, v: string) =>
    onChange({ ...value, [name]: v });

  if (dateParams.length === 0) {
    return <div className="text-xs text-ink-400">Template has no date parameters.</div>;
  }

  return (
    <div className="space-y-3">
      {rangeKeys && (
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
          <span
            className={
              'px-2.5 py-1 text-xs rounded-md border ' +
              (active === null
                ? 'bg-ink-900 text-white border-ink-900'
                : 'bg-white text-ink-500 border-ink-200')
            }
            title="Custom dates — edit the inputs below"
          >
            Custom
          </span>
        </div>
      )}

      <div className={`grid gap-3 ${dateParams.length > 1 ? 'grid-cols-2' : 'grid-cols-1'}`}>
        {dateParams.map(name => (
          <div key={name}>
            <label className="label font-mono">{name}</label>
            <input
              className="input"
              type="date"
              value={value[name] ?? ''}
              onChange={e => setOne(name, e.target.value)}
            />
          </div>
        ))}
      </div>

      <div className="text-[11px] text-ink-400">
        Dates interpreted in GMT+3 (MT5 server time).
      </div>
    </div>
  );
}
