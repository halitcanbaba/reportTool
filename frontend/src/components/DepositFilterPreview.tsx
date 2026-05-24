import { useEffect, useState } from 'react';
import { DepositFiltersAPI, type DepositFilterPreviewRequest, type DepositFilterPreviewResult } from '../api/depositFilters';
import { ManagersAPI } from '../api/managers';
import { AccountFilterPicker } from './AccountFilterPicker';
import { fmtInt, fmtNumber, fmtDateTime } from '../utils/format';
import { todayLocal, todayLocalMinus } from '../utils/format';
import type { DepositFilterBucket, Manager } from '../types';

type Props = {
  buckets: DepositFilterBucket[];   // the in-flight buckets being designed; rows tagged with each one that matches
};

const PAGE_SIZE = 50;

export function DepositFilterPreview({ buckets }: Props) {
  const [managers, setManagers] = useState<Manager[]>([]);
  const [managerId, setManagerId] = useState<number | null>(null);
  const [accountFilterId, setAccountFilterId] = useState<number | null>(null);
  const [dateFrom, setDateFrom] = useState<string>(() => todayLocalMinus(7));
  const [dateTo,   setDateTo]   = useState<string>(() => todayLocal());

  const [loading, setLoading]       = useState(false);
  const [csvLoading, setCsvLoading] = useState(false);
  const [error, setError]           = useState<string | null>(null);
  const [result, setResult]         = useState<DepositFilterPreviewResult | null>(null);
  const [activeSpec, setActiveSpec] = useState<Omit<DepositFilterPreviewRequest, 'buckets' | 'offset' | 'limit'> | null>(null);
  const [page, setPage]             = useState(1);

  useEffect(() => {
    ManagersAPI.list().then(setManagers).catch(() => {});
  }, []);

  //--- Re-fetch when buckets change (user edited a rule) so matched chips
  //--- update live without a manual "Preview matches" click.
  useEffect(() => {
    if (!activeSpec) return;
    const spec: DepositFilterPreviewRequest = {
      ...activeSpec,
      buckets: buckets.map(b => ({ key: b.key, label: b.label, predicate: b.predicate })),
      offset: (page - 1) * PAGE_SIZE,
      limit:  PAGE_SIZE,
    };
    setLoading(true); setError(null);
    DepositFiltersAPI.preview(spec)
      .then(setResult)
      .catch(e => setError(e.message ?? 'preview failed'))
      .finally(() => setLoading(false));
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [buckets]);

  const buildSpec = (): Omit<DepositFilterPreviewRequest, 'buckets' | 'offset' | 'limit'> | null => {
    if (managerId == null) return null;
    return {
      manager_id: managerId,
      account_filter_id: accountFilterId,
      date_from: dateFrom,
      date_to:   dateTo,
    };
  };

  const fetchPage = async (spec: Omit<DepositFilterPreviewRequest, 'buckets' | 'offset' | 'limit'>, p: number) => {
    setLoading(true); setError(null);
    try {
      const r = await DepositFiltersAPI.preview({
        ...spec,
        buckets: buckets.map(b => ({ key: b.key, label: b.label, predicate: b.predicate })),
        offset: (p - 1) * PAGE_SIZE,
        limit:  PAGE_SIZE,
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
    const s = buildSpec();
    if (!s) { setError('Select a manager first.'); return; }
    setActiveSpec(s);
    await fetchPage(s, 1);
  };

  const goPage = (p: number) => activeSpec && fetchPage(activeSpec, p);

  const onDownloadCsv = async () => {
    const s = buildSpec();
    if (!s) { setError('Select a manager first.'); return; }
    setCsvLoading(true); setError(null);
    try {
      const blob = await DepositFiltersAPI.previewCsv({
        ...s,
        buckets: buckets.map(b => ({ key: b.key, label: b.label, predicate: b.predicate })),
      });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `deposit_filter_preview_${new Date().toISOString().slice(0, 10)}.csv`;
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

  const total = result?.total_count ?? 0;
  const lastPage = Math.max(1, Math.ceil(total / PAGE_SIZE));
  const startRow = total === 0 ? 0 : (page - 1) * PAGE_SIZE + 1;
  const endRow   = Math.min(total, page * PAGE_SIZE);
  const canPrev  = page > 1 && !loading;
  const canNext  = page < lastPage && !loading;

  //--- Bucket-key → display label lookup for the row badges.
  const labelByKey = new Map(buckets.map(b => [b.key, b.label]));

  return (
    <div className="space-y-3">
      <div className="grid grid-cols-1 md:grid-cols-4 gap-3">
        <div>
          <label className="label">Manager</label>
          <select className="input" value={managerId ?? ''}
                  onChange={e => setManagerId(e.target.value ? Number(e.target.value) : null)}>
            <option value="">— select —</option>
            {managers.map(m => <option key={m.id} value={m.id}>{m.name} ({m.brand})</option>)}
          </select>
        </div>
        <div>
          <label className="label">Account filter (optional)</label>
          <AccountFilterPicker value={accountFilterId} managerId={managerId} onChange={setAccountFilterId} />
        </div>
        <div>
          <label className="label">From</label>
          <input className="input" type="date" value={dateFrom} onChange={e => setDateFrom(e.target.value)} />
        </div>
        <div>
          <label className="label">To</label>
          <input className="input" type="date" value={dateTo} onChange={e => setDateTo(e.target.value)} />
        </div>
      </div>

      <div className="flex items-center gap-3 flex-wrap">
        <button type="button" className="btn-primary text-sm"
                disabled={loading || managerId == null} onClick={onPreview}>
          {loading && !csvLoading ? 'Loading…' : 'Preview matches'}
        </button>
        <button type="button" className="btn-secondary text-sm"
                disabled={csvLoading || managerId == null} onClick={onDownloadCsv}
                title="Download every cash-flow deal in the window as CSV with matched-bucket column">
          {csvLoading ? 'Preparing CSV…' : '⬇ Download CSV'}
        </button>
        {managerId == null && <span className="text-xs text-amber-700">Select a manager to enable preview.</span>}
        {error && <span className="text-xs text-red-600 font-mono">{error}</span>}
      </div>

      {result && (
        <div className="border border-ink-200 rounded">
          <div className="px-3 py-2 bg-ink-50 border-b border-ink-100 text-sm flex items-center gap-3 flex-wrap">
            <span className="font-semibold">{fmtInt(total)} cash-flow deals</span>
            {result.buckets.length > 0 && (
              <span className="flex items-center gap-1.5 flex-wrap">
                <span className="text-ink-400">·</span>
                {result.buckets.map(b => (
                  <span key={b.key} className="inline-flex items-center gap-1 text-xs">
                    <span className="font-mono text-ink-700">{b.key}</span>
                    <span className="font-mono text-emerald-700 font-semibold">{fmtInt(b.matched_count)}</span>
                  </span>
                ))}
              </span>
            )}
            {total > 0 && (
              <span className="text-ink-500">· showing {startRow}–{endRow}</span>
            )}
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
            <div className="p-6 text-center text-ink-400 text-sm">No cash-flow deals found in this window.</div>
          ) : (
            <table className="min-w-full text-xs">
              <thead className="bg-ink-50/50">
                <tr>
                  <th className="px-3 py-1.5 text-left  font-medium text-ink-600 uppercase tracking-wide">Time (UTC)</th>
                  <th className="px-3 py-1.5 text-right font-medium text-ink-600 uppercase tracking-wide">Login</th>
                  <th className="px-3 py-1.5 text-left  font-medium text-ink-600 uppercase tracking-wide">Action</th>
                  <th className="px-3 py-1.5 text-right font-medium text-ink-600 uppercase tracking-wide">Amount</th>
                  <th className="px-3 py-1.5 text-left  font-medium text-ink-600 uppercase tracking-wide">Comment</th>
                  <th className="px-3 py-1.5 text-left  font-medium text-ink-600 uppercase tracking-wide">Matched buckets</th>
                </tr>
              </thead>
              <tbody>
                {result.rows.map((r, i) => (
                  <tr key={i}
                      className={'border-t border-ink-50 ' + (r.matched_buckets.length > 0 ? 'bg-emerald-50/40' : '')}>
                    <td className="px-3 py-1 font-mono text-ink-700 whitespace-nowrap">{fmtDateTime(r.time)}</td>
                    <td className="px-3 py-1 text-right font-mono tabular-nums">{r.login}</td>
                    <td className="px-3 py-1 font-mono text-[11px] text-ink-600">{r.action_label}</td>
                    <td className={'px-3 py-1 text-right font-mono tabular-nums ' + (r.profit < 0 ? 'text-red-600' : '')}>
                      {fmtNumber(r.profit)}
                    </td>
                    <td className="px-3 py-1 font-mono text-[11px] text-ink-800">{r.comment}</td>
                    <td className="px-3 py-1">
                      {r.matched_buckets.length === 0 ? (
                        <span className="text-ink-300">—</span>
                      ) : (
                        <span className="flex flex-wrap gap-1">
                          {r.matched_buckets.map(k => (
                            <span key={k}
                                  className="inline-block px-1.5 py-0.5 text-[10px] font-semibold bg-emerald-600 text-white rounded"
                                  title={labelByKey.get(k) ?? k}>
                              {k}
                            </span>
                          ))}
                        </span>
                      )}
                    </td>
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
