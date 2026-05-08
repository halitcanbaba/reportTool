import { useState } from 'react';
import { isValidRegex, testRegex } from '../utils/regex';

export function RegexListEditor({
  label, value, onChange, hint,
}: {
  label: string;
  value: string[];
  onChange: (next: string[]) => void;
  hint?: string;
}) {
  const [sample, setSample] = useState('');

  const update = (idx: number, next: string) => {
    const copy = value.slice(); copy[idx] = next; onChange(copy);
  };
  const remove = (idx: number) => onChange(value.filter((_, i) => i !== idx));
  const add = () => onChange([...value, '']);

  return (
    <div className="card p-4">
      <div className="flex items-center justify-between mb-3">
        <div>
          <div className="font-medium text-ink-900">{label}</div>
          {hint && <div className="text-xs text-ink-500 mt-0.5">{hint}</div>}
        </div>
        <button type="button" className="btn-secondary text-xs px-3 py-1" onClick={add}>+ add pattern</button>
      </div>

      <div className="space-y-2">
        {value.length === 0 && <div className="text-sm text-ink-400 italic">No patterns. Click "add pattern".</div>}
        {value.map((p, i) => {
          const v = isValidRegex(p);
          const matches = sample && p ? testRegex(p, sample) : false;
          return (
            <div key={i} className="flex items-start gap-2">
              <div className="flex-1">
                <input
                  className={`input font-mono text-xs ${!v.ok ? 'border-red-400 focus:ring-red-500' : ''}`}
                  value={p}
                  onChange={e => update(i, e.target.value)}
                  placeholder="(?i)deposit|yatirim"
                  spellCheck={false}
                />
                {!v.ok && <div className="text-xs text-red-600 mt-1">{v.error}</div>}
                {sample && p && v.ok && (
                  <div className={`text-xs mt-1 ${matches ? 'text-emerald-700' : 'text-ink-400'}`}>
                    {matches ? '✓ matches sample' : '✗ no match'}
                  </div>
                )}
              </div>
              <button type="button" onClick={() => remove(i)}
                      className="btn-secondary text-xs px-2 py-2 text-red-600 hover:bg-red-50">
                ×
              </button>
            </div>
          );
        })}
      </div>

      <div className="mt-3 pt-3 border-t border-ink-100">
        <label className="label">Test against a sample comment</label>
        <input className="input text-xs" value={sample} onChange={e => setSample(e.target.value)}
               placeholder="paste a real deal comment to test live ..." />
      </div>
    </div>
  );
}
