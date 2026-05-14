import { useState } from 'react';
import { AccountFiltersAPI, type AccountFilterPreviewResult } from '../api/accountFilters';
import { fmtInt } from '../utils/format';
import type { Predicate } from '../types';

type Props = {
  managerId: number | null;
  groupMasks: string[];
  groupRegex: string;
  loginMin: number | null;
  loginMax: number | null;
  userPredicate?: Predicate | null;
};

export function AccountFilterPreview({ managerId, groupMasks, groupRegex, loginMin, loginMax, userPredicate }: Props) {
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<AccountFilterPreviewResult | null>(null);

  const run = async () => {
    if (managerId == null) { setError('Select a manager first.'); return; }
    setLoading(true); setError(null);
    try {
      const r = await AccountFiltersAPI.preview({
        manager_id: managerId,
        group_masks: groupMasks,
        group_regex: groupRegex,
        login_min: loginMin,
        login_max: loginMax,
        user_predicate: userPredicate ?? null,
      });
      setResult(r);
    } catch (e: any) {
      setError(e.message ?? 'preview failed');
      setResult(null);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="space-y-3">
      <div className="flex items-center gap-3">
        <button type="button" className="btn-primary text-sm" disabled={loading || managerId == null} onClick={run}>
          {loading ? 'Loading…' : 'Preview matches'}
        </button>
        {managerId == null && <span className="text-xs text-amber-700">Select a manager to enable preview.</span>}
        {error && <span className="text-xs text-red-600 font-mono">{error}</span>}
      </div>

      {result && (
        <div className="border border-ink-200 rounded">
          <div className="px-3 py-2 bg-ink-50 border-b border-ink-100 text-sm">
            <span className="font-semibold">Matched {fmtInt(result.matched_count)} accounts</span>
            <span className="text-ink-500"> · showing first {result.sample_logins.length}</span>
            <span className="text-ink-500"> · {result.sample_groups.length} distinct group{result.sample_groups.length === 1 ? '' : 's'} sampled</span>
          </div>
          {result.matched_count === 0 ? (
            <div className="p-6 text-center text-ink-400 text-sm">No accounts matched this filter.</div>
          ) : (
            <table className="min-w-full text-xs">
              <thead className="bg-ink-50/50">
                <tr>
                  <th className="px-3 py-1.5 text-right font-medium text-ink-600 uppercase tracking-wide">Login</th>
                  <th className="px-3 py-1.5 text-left font-medium text-ink-600 uppercase tracking-wide">Group</th>
                  <th className="px-3 py-1.5 text-left font-medium text-ink-600 uppercase tracking-wide">Name</th>
                  {(result.extra_fields ?? []).map(f => (
                    <th key={f} className="px-3 py-1.5 text-left font-medium text-emerald-700 uppercase tracking-wide bg-emerald-50/40">{f}</th>
                  ))}
                </tr>
              </thead>
              <tbody>
                {result.sample_logins.map(r => (
                  <tr key={r.login} className="border-t border-ink-50">
                    <td className="px-3 py-1 text-right font-mono tabular-nums">{r.login}</td>
                    <td className="px-3 py-1 font-mono">{r.group}</td>
                    <td className="px-3 py-1">{r.name}</td>
                    {(result.extra_fields ?? []).map(f => (
                      <td key={f} className="px-3 py-1 font-mono text-ink-700 bg-emerald-50/20">{r.extra?.[f] ?? ''}</td>
                    ))}
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}
    </div>
  );
}
