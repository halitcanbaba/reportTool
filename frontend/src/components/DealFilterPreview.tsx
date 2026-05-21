import { useEffect, useState } from 'react';
import { DealFiltersAPI, type DealFilterPreviewRequest, type DealFilterPreviewResult } from '../api/dealFilters';
import { ManagersAPI } from '../api/managers';
import { AccountFilterPicker } from './AccountFilterPicker';
import { fmtInt, fmtNumber, fmtDateTime } from '../utils/format';
import { todayLocal, todayLocalMinus } from '../utils/format';
import type { Manager, Predicate } from '../types';

type Props = {
  predicate: Predicate | null;     // the candidate being designed; preview tags each row with match/no-match
};

const PAGE_SIZE = 50;

export function DealFilterPreview({ predicate }: Props) {
  const [managers, setManagers] = useState<Manager[]>([]);
  const [managerId, setManagerId] = useState<number | null>(null);
  const [accountFilterId, setAccountFilterId] = useState<number | null>(null);
  const [dateFrom, setDateFrom] = useState<string>(() => todayLocalMinus(7));
  const [dateTo,   setDateTo]   = useState<string>(() => todayLocal());

  const [loading, setLoading]       = useState(false);
  const [csvLoading, setCsvLoading] = useState(false);
  const [error, setError]           = useState<string | null>(null);
  const [result, setResult]         = useState<DealFilterPreviewResult | null>(null);
  const [activeSpec, setActiveSpec] = useState<DealFilterPreviewRequest | null>(null);
  const [page, setPage]             = useState(1);

  useEffect(() => {
    ManagersAPI.list().then(setManagers).catch(() => {});
  }, []);

  //--- When the candidate predicate changes (user edited rules), re-fetch the
  //--- last active spec so matched badges update without re-clicking Preview.
  useEffect(() => {
    if (!activeSpec) return;
    const spec: DealFilterPreviewRequest = { ...activeSpec, predicate, offset: (page - 1) * PAGE_SIZE, limit: PAGE_SIZE };
    setLoading(true); setError(null);
    DealFiltersAPI.preview(spec)
      .then(setResult)
      .catch(e => setError(e.message ?? 'preview failed'))
      .finally(() => setLoading(false));
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [predicate]);

  const buildSpec = (): DealFilterPreviewRequest | null => {
    if (managerId == null) return null;
    return {
      manager_id: managerId,
      account_filter_id: accountFilterId,
      date_from: dateFrom,
      date_to:   dateTo,
      predicate: predicate ?? null,
    };
  };

  const fetchPage = async (spec: DealFilterPreviewRequest, p: number) => {
    setLoading(true); setError(null);
    try {
      const r = await DealFiltersAPI.preview({ ...spec, offset: (p - 1) * PAGE_SIZE, limit: PAGE_SIZE });
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
      const blob = await DealFiltersAPI.previewCsv(s);
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `deal_filter_preview_${new Date().toISOString().slice(0, 10)}.csv`;
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
  const matched = result?.matched_count ?? 0;
  const lastPage = Math.max(1, Math.ceil(total / PAGE_SIZE));
  const startRow = total === 0 ? 0 : (page - 1) * PAGE_SIZE + 1;
  const endRow   = Math.min(total, page * PAGE_SIZE);
  const canPrev  = page > 1 && !loading;
  const canNext  = page < lastPage && !loading;

  return (
    <div className="space-y-3">
      {/*--- Filter spec inputs ---*/}
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
                title="Download every cash-flow deal in the window as CSV">
          {csvLoading ? 'Preparing CSV…' : '⬇ Download CSV'}
        </button>
        {managerId == null && <span className="text-xs text-amber-700">Select a manager to enable preview.</span>}
        {error && <span className="text-xs text-red-600 font-mono">{error}</span>}
      </div>

      {result && (
        <div className="border border-ink-200 rounded">
          <div className="px-3 py-2 bg-ink-50 border-b border-ink-100 text-sm flex items-center gap-3 flex-wrap">
            <span className="font-semibold">{fmtInt(total)} cash-flow deals</span>
            <span className="text-emerald-700">· {fmtInt(matched)} matched</span>
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
                  <th className="px-3 py-1.5 text-center font-medium text-ink-600 uppercase tracking-wide">Match</th>
                </tr>
              </thead>
              <tbody>
                {result.rows.map((r, i) => (
                  <tr key={i}
                      className={'border-t border-ink-50 ' + (r.matched ? 'bg-emerald-50/40' : '')}>
                    <td className="px-3 py-1 font-mono text-ink-700 whitespace-nowrap">{fmtDateTime(r.time)}</td>
                    <td className="px-3 py-1 text-right font-mono tabular-nums">{r.login}</td>
                    <td className="px-3 py-1 font-mono text-[11px] text-ink-600">{r.action_label}</td>
                    <td className={'px-3 py-1 text-right font-mono tabular-nums ' + (r.profit < 0 ? 'text-red-600' : '')}>
                      {fmtNumber(r.profit)}
                    </td>
                    <td className="px-3 py-1 font-mono text-[11px] text-ink-800">{r.comment}</td>
                    <td className="px-3 py-1 text-center">
                      {r.matched
                        ? <span className="inline-block px-1.5 py-0.5 text-[10px] font-semibold bg-emerald-600 text-white rounded">✓</span>
                        : <span className="text-ink-300">—</span>}
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
