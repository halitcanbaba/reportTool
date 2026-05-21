import { useState } from 'react';
import { AccountFiltersAPI, type AccountFilterPreviewRequest, type AccountFilterPreviewResult } from '../api/accountFilters';
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

const PAGE_SIZE = 50;

export function AccountFilterPreview({ managerId, groupMasks, groupRegex, loginMin, loginMax, userPredicate }: Props) {
  const [loading, setLoading] = useState(false);
  const [csvLoading, setCsvLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [result, setResult] = useState<AccountFilterPreviewResult | null>(null);
  //--- Filter spec snapshotted at the last "Preview matches" click. Page nav
  //--- re-runs against this snapshot so editing the filter mid-pagination
  //--- doesn't cause page-2 to return rows from a different filter than page-1.
  const [activeFilter, setActiveFilter] = useState<AccountFilterPreviewRequest | null>(null);
  const [page, setPage] = useState(1);

  const buildFilter = (): AccountFilterPreviewRequest | null => {
    if (managerId == null) return null;
    return {
      manager_id: managerId,
      group_masks: groupMasks,
      group_regex: groupRegex,
      login_min: loginMin,
      login_max: loginMax,
      user_predicate: userPredicate ?? null,
    };
  };

  const fetchPage = async (filter: AccountFilterPreviewRequest, p: number) => {
    setLoading(true); setError(null);
    try {
      const r = await AccountFiltersAPI.preview({
        ...filter,
        offset: (p - 1) * PAGE_SIZE,
        limit: PAGE_SIZE,
      });
      setResult(r);
      setPage(p);
    } catch (e: any) {
      setError(e.message ?? 'preview failed');
      setResult(null);
    } finally {
      setLoading(false);
    }
  };

  const onPreview = async () => {
    const f = buildFilter();
    if (!f) { setError('Select a manager first.'); return; }
    setActiveFilter(f);
    await fetchPage(f, 1);
  };

  const goPage = async (p: number) => {
    if (!activeFilter) return;
    await fetchPage(activeFilter, p);
  };

  const onDownloadCsv = async () => {
    const f = buildFilter();
    if (!f) { setError('Select a manager first.'); return; }
    setCsvLoading(true); setError(null);
    try {
      const blob = await AccountFiltersAPI.previewCsv(f);
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `account_filter_preview_${new Date().toISOString().slice(0, 10)}.csv`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);
    } catch (e: any) {
      setError(e.message ?? 'csv download failed');
    } finally {
      setCsvLoading(false);
    }
  };

  //--- Derived pagination figures (1-based, inclusive end).
  const total = result?.matched_count ?? 0;
  const lastPage = Math.max(1, Math.ceil(total / PAGE_SIZE));
  const startRow = total === 0 ? 0 : (page - 1) * PAGE_SIZE + 1;
  const endRow   = Math.min(total, page * PAGE_SIZE);
  const canPrev  = page > 1 && !loading;
  const canNext  = page < lastPage && !loading;

  return (
    <div className="space-y-3">
      <div className="flex items-center gap-3 flex-wrap">
        <button type="button" className="btn-primary text-sm"
                disabled={loading || managerId == null} onClick={onPreview}>
          {loading && !csvLoading ? 'Loading…' : 'Preview matches'}
        </button>
        <button type="button" className="btn-secondary text-sm"
                disabled={csvLoading || managerId == null} onClick={onDownloadCsv}
                title="Download every matched account as CSV">
          {csvLoading ? 'Preparing CSV…' : '⬇ Download CSV'}
        </button>
        {managerId == null && <span className="text-xs text-amber-700">Select a manager to enable preview.</span>}
        {error && <span className="text-xs text-red-600 font-mono">{error}</span>}
      </div>

      {result && (
        <div className="border border-ink-200 rounded">
          <div className="px-3 py-2 bg-ink-50 border-b border-ink-100 text-sm flex items-center gap-3 flex-wrap">
            <span className="font-semibold">Matched {fmtInt(total)} accounts</span>
            {total > 0 && (
              <span className="text-ink-500">· showing {startRow}–{endRow}</span>
            )}
            <span className="text-ink-500">· {result.sample_groups.length} distinct group{result.sample_groups.length === 1 ? '' : 's'} sampled</span>
            {total > PAGE_SIZE && (
              <span className="ml-auto inline-flex items-center gap-1">
                <button type="button" className="btn-secondary text-xs px-2 py-1 disabled:opacity-40"
                        disabled={!canPrev} onClick={() => goPage(page - 1)}>← Prev</button>
                <span className="text-xs text-ink-600 tabular-nums px-1">Page {page} / {lastPage}</span>
                <button type="button" className="btn-secondary text-xs px-2 py-1 disabled:opacity-40"
                        disabled={!canNext} onClick={() => goPage(page + 1)}>Next →</button>
              </span>
            )}
          </div>
          {total === 0 ? (
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
