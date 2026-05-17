import { useEffect, useMemo, useRef, useState } from 'react';
import { useParams } from 'react-router-dom';
import { useReportJob } from '../hooks/useReportJob';
import { ReportsAPI, type SendTelegramOpts } from '../api/reports';
import { SettingsAPI } from '../api/settings';
import { useAuth } from '../contexts/AuthContext';
import { StatusPill } from '../components/StatusPill';
import { Breadcrumbs } from '../components/Breadcrumbs';
import { fmtDateTime, fmtMoney, fmtPct, fmtInt, fmtDate } from '../utils/format';
import {
  toXlsxBlob, toPdfBlob, toScreenshotBlob, fetchCsvBlob,
  buildTextSummary, safeBaseName,
} from '../lib/reportExport';
import type { ResultPreview } from '../types';

const fmtCell = (v: number | string | null, fmt: string): string => {
  if (v == null) return '';
  if (typeof v === 'string') return v;
  switch (fmt) {
    case 'money': return fmtMoney(v);
    case 'pct':   return fmtPct(v);
    case 'int':   return fmtInt(v);
    case 'date':  return fmtDate(v);
    default:      return String(v);
  }
};

function parseRunSummary(params_json: string): { dates: [string, string][]; topN?: number } {
  try {
    const j = JSON.parse(params_json);
    const d = j?.dates && typeof j.dates === 'object' ? j.dates : {};
    const dates = Object.entries(d).filter(([, v]) => typeof v === 'string') as [string, string][];
    const topN = typeof j?.top_n === 'number' ? j.top_n : undefined;
    return { dates, topN };
  } catch { return { dates: [] }; }
}

function pickFromTo(dates: [string, string][]): { from?: string; to?: string } | null {
  if (dates.length === 0) return null;
  const map = new Map(dates);
  const from = map.get('date_from') ?? (dates[0]?.[1]);
  const to   = map.get('date_to')   ?? (dates.length >= 2 ? dates[1][1] : undefined);
  if (!from) return null;
  return { from, to };
}

//--- Telegram caps: 50 MB document, 10 MB photo.
const TG_DOCUMENT_MAX = 50 * 1024 * 1024;
const TG_PHOTO_MAX    = 10 * 1024 * 1024;

type ExportFormat = 'csv' | 'xlsx' | 'pdf' | 'screenshot' | 'text';

export function ResultViewPage() {
  const { id } = useParams();
  const jobId = id != null ? Number(id) : null;
  const { job, error } = useReportJob(jobId);
  const { user } = useAuth();
  const isAdmin = user?.role === 'admin';

  const tableRef = useRef<HTMLDivElement>(null);
  const [telegramConfigured, setTelegramConfigured] = useState<boolean | null>(null);

  //--- One-shot fetch of Telegram settings so we can enable/disable Send items
  //--- with a helpful tooltip when no bot token is configured.
  useEffect(() => {
    if (!isAdmin) return;
    SettingsAPI.telegramGet()
      .then(s => setTelegramConfigured(s.configured))
      .catch(() => setTelegramConfigured(false));
  }, [isAdmin]);

  if (error) return <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm">{error}</div>;
  if (!job)  return <div className="text-sm text-ink-400">Loading…</div>;

  const preview = job.preview;
  const { dates, topN } = parseRunSummary(job.params_json);
  const range = pickFromTo(dates);
  const templateName = preview?.template_name ?? job.template_name ?? 'Template';
  const baseName = safeBaseName(`${templateName}${range?.from ? `_${range.from}` : ''}${range?.to && range.to !== range.from ? `_${range.to}` : ''}`);

  return (
    <div className="space-y-4">
      <div className="flex items-start justify-between">
        <div>
          <Breadcrumbs items={[
            { label: 'History', to: '/history' },
            { label: `Job #${job.id} — ${templateName}` },
          ]} />
          <h1 className="text-2xl font-semibold text-ink-900">
            Job #{job.id}: {templateName}
          </h1>
          {range && (
            <div className="mt-1 text-sm text-ink-700">
              Ran <span className="font-semibold">{templateName}</span> for{' '}
              <span className="font-mono">{range.from}</span>
              {range.to && range.to !== range.from && (
                <> → <span className="font-mono">{range.to}</span></>
              )}
            </div>
          )}
          <div className="mt-1 flex flex-wrap items-center gap-x-3 gap-y-1 text-sm text-ink-600">
            <StatusPill status={job.status} />
            {dates.map(([name, val]) => (
              <span key={name} className="inline-flex items-center gap-1">
                <span className="text-ink-500 font-mono text-xs">{name}</span>
                <span className="font-mono">{val}</span>
              </span>
            ))}
            {topN !== undefined && topN > 0 && (
              <span className="inline-flex items-center gap-1">
                <span className="text-ink-500 font-mono text-xs">top</span>
                <span className="font-mono">{topN}</span>
              </span>
            )}
          </div>
          <div className="mt-0.5 text-[11px] text-ink-400">
            created {fmtDateTime(job.created_at)}
            {job.completed_at ? <> · finished {fmtDateTime(job.completed_at)}</> : null}
          </div>
        </div>
        <div className="flex gap-2 items-start">
          {preview && (
            <DownloadMenu
              jobId={job.id}
              preview={preview}
              baseName={baseName}
              templateName={templateName}
              csvUrl={job.csv_url ? ReportsAPI.csvUrl(job.id) : null}
            />
          )}
          {preview && isAdmin && (
            <SendTelegramMenu
              jobId={job.id}
              preview={preview}
              baseName={baseName}
              templateName={templateName}
              range={range}
              tableRef={tableRef}
              configured={telegramConfigured}
            />
          )}
        </div>
      </div>

      {job.status === 'queued' || job.status === 'running' ? (
        <div className="card p-6">
          <div className="text-sm text-ink-600">In progress…</div>
          <div className="mt-2 w-full bg-ink-100 rounded h-2 overflow-hidden">
            <div className="bg-blue-500 h-full transition-all" style={{ width: `${Math.round((job.progress || 0) * 100)}%` }} />
          </div>
          <div className="text-xs text-ink-500 mt-1 text-right">{Math.round((job.progress || 0) * 100)}%</div>
        </div>
      ) : null}

      {job.status === 'failed' && (
        <div className="card p-4 border-red-200 bg-red-50 text-red-800 text-sm font-mono">
          {job.error_message || 'job failed'}
        </div>
      )}

      {preview && preview.columns && (
        <div ref={tableRef} className="card overflow-auto">
          <table className="min-w-full text-sm">
            <thead className="bg-ink-50 border-b border-ink-100">
              <tr>
                {preview.columns.map(c => (
                  <th key={c.key} className={`px-3 py-2 text-${c.format === 'text' ? 'left' : 'right'} font-medium text-ink-600 uppercase text-xs tracking-wide`}>
                    {c.label}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {preview.rows.map((row, i) => (
                <tr key={i} className="border-b border-ink-50 last:border-0">
                  {preview.columns.map((c, k) => {
                    const cell = row[k];
                    const align = c.format === 'text' ? 'text-left' : 'text-right tabular-nums';
                    return (
                      <td key={c.key} className={`px-3 py-2 ${align} ${c.format === 'text' ? '' : 'font-mono'}`}>
                        {fmtCell(cell ?? null, c.format)}
                      </td>
                    );
                  })}
                </tr>
              ))}
              {preview.rows.length === 0 && (
                <tr><td className="px-3 py-12 text-center text-ink-400" colSpan={preview.columns.length}>No rows.</td></tr>
              )}
            </tbody>
          </table>
        </div>
      )}

      {preview && (
        <div className="text-xs text-ink-500">
          Total logins evaluated: {preview.total_logins}
          {preview.rows.length === 200 && ' · preview limited to first 200 rows; download CSV for full set'}
        </div>
      )}
    </div>
  );
}

//+------------------------------------------------------------------+
//| Download dropdown — produces the chosen blob (locally for XLSX /  |
//| PDF, server fetch for CSV) and pushes it through a hidden anchor. |
//+------------------------------------------------------------------+

function DownloadMenu({ jobId, preview, baseName, templateName, csvUrl }: {
  jobId: number;
  preview: ResultPreview;
  baseName: string;
  templateName: string;
  csvUrl: string | null;
}) {
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState<ExportFormat | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('mousedown', onDown);
    return () => document.removeEventListener('mousedown', onDown);
  }, [open]);

  const triggerDownload = (blob: Blob, filename: string) => {
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url; a.download = filename; document.body.appendChild(a);
    a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
  };

  const pick = async (fmt: 'csv' | 'xlsx' | 'pdf') => {
    setBusy(fmt); setErr(null);
    try {
      if (fmt === 'csv') {
        if (csvUrl) {
          //--- Easy path — server already has the file.
          const a = document.createElement('a');
          a.href = csvUrl; a.download = `${baseName}.csv`; document.body.appendChild(a);
          a.click(); a.remove();
        } else {
          const blob = await fetchCsvBlob(jobId);
          triggerDownload(blob, `${baseName}.csv`);
        }
      } else if (fmt === 'xlsx') {
        const blob = await toXlsxBlob(preview);
        triggerDownload(blob, `${baseName}.xlsx`);
      } else {
        const blob = await toPdfBlob(preview, templateName);
        triggerDownload(blob, `${baseName}.pdf`);
      }
      setOpen(false);
    } catch (e: any) {
      setErr(e?.message ?? 'download failed');
    } finally {
      setBusy(null);
    }
  };

  return (
    <div ref={ref} className="relative">
      <button type="button" className="btn-primary text-sm" onClick={() => setOpen(o => !o)}>
        Download {open ? '▴' : '▾'}
      </button>
      {open && (
        <div className="absolute z-30 right-0 mt-1 w-48 bg-white border border-ink-200 rounded shadow-lg p-1">
          <MenuItem label="CSV"  disabled={busy !== null} busy={busy === 'csv'}  onClick={() => pick('csv')} />
          <MenuItem label="XLSX" disabled={busy !== null} busy={busy === 'xlsx'} onClick={() => pick('xlsx')} />
          <MenuItem label="PDF"  disabled={busy !== null} busy={busy === 'pdf'}  onClick={() => pick('pdf')} />
          {err && <div className="text-[11px] text-red-600 px-2 py-1">{err}</div>}
        </div>
      )}
    </div>
  );
}

function MenuItem({ label, onClick, disabled, busy, hint }: {
  label: string;
  onClick: () => void;
  disabled?: boolean;
  busy?: boolean;
  hint?: string;
}) {
  return (
    <button type="button"
            onClick={onClick}
            disabled={disabled}
            title={hint}
            className="w-full text-left px-3 py-1.5 text-sm rounded hover:bg-ink-50 disabled:opacity-50 disabled:cursor-not-allowed flex items-center justify-between">
      <span>{label}</span>
      {busy && <span className="text-[10px] text-ink-400">…</span>}
    </button>
  );
}

//+------------------------------------------------------------------+
//| Send-to-Telegram dropdown + chat/caption modal.                   |
//+------------------------------------------------------------------+

function SendTelegramMenu({ jobId, preview, baseName, templateName, range, tableRef, configured }: {
  jobId: number;
  preview: ResultPreview;
  baseName: string;
  templateName: string;
  range: { from?: string; to?: string } | null;
  tableRef: React.RefObject<HTMLDivElement>;
  configured: boolean | null;
}) {
  const [open, setOpen] = useState(false);
  const [picked, setPicked] = useState<ExportFormat | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('mousedown', onDown);
    return () => document.removeEventListener('mousedown', onDown);
  }, [open]);

  const disabledHint = configured === false ? 'Configure Telegram in Settings first' : undefined;

  const defaultCaption = useMemo(() => {
    const parts = [templateName];
    if (range?.from) parts.push(`${range.from}${range.to && range.to !== range.from ? ` → ${range.to}` : ''}`);
    return parts.join(' · ');
  }, [templateName, range]);

  const pick = (fmt: ExportFormat) => {
    setPicked(fmt);
    setOpen(false);
  };

  return (
    <>
      <div ref={ref} className="relative">
        <button type="button" className="btn-secondary text-sm" onClick={() => setOpen(o => !o)}>
          Send to Telegram {open ? '▴' : '▾'}
        </button>
        {open && (
          <div className="absolute z-30 right-0 mt-1 w-56 bg-white border border-ink-200 rounded shadow-lg p-1">
            <MenuItem label="CSV file"        disabled={configured === false} hint={disabledHint} onClick={() => pick('csv')} />
            <MenuItem label="XLSX file"       disabled={configured === false} hint={disabledHint} onClick={() => pick('xlsx')} />
            <MenuItem label="PDF file"        disabled={configured === false} hint={disabledHint} onClick={() => pick('pdf')} />
            <MenuItem label="Screenshot"      disabled={configured === false} hint={disabledHint} onClick={() => pick('screenshot')} />
            <MenuItem label="Text summary"    disabled={configured === false} hint={disabledHint} onClick={() => pick('text')} />
            {configured === false && (
              <div className="text-[11px] text-ink-500 px-2 py-1 italic">{disabledHint}</div>
            )}
          </div>
        )}
      </div>

      {picked && (
        <SendDialog
          format={picked}
          jobId={jobId}
          preview={preview}
          baseName={baseName}
          templateName={templateName}
          range={range}
          tableRef={tableRef}
          defaultCaption={defaultCaption}
          onClose={() => setPicked(null)}
        />
      )}
    </>
  );
}

function SendDialog({ format, jobId, preview, baseName, templateName, range, tableRef, defaultCaption, onClose }: {
  format: ExportFormat;
  jobId: number;
  preview: ResultPreview;
  baseName: string;
  templateName: string;
  range: { from?: string; to?: string } | null;
  tableRef: React.RefObject<HTMLDivElement>;
  defaultCaption: string;
  onClose: () => void;
}) {
  const [chatId, setChatId] = useState('');
  const [caption, setCaption] = useState(defaultCaption);
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ kind: 'ok' | 'err'; text: string } | null>(null);

  const captionMax = 1024;
  const send = async () => {
    setBusy(true); setMsg(null);
    try {
      let opts: SendTelegramOpts;
      if (format === 'text') {
        const text = buildTextSummary(preview, templateName, range, preview.total_logins);
        opts = { kind: 'text', text, chatId: chatId || undefined };
      } else if (format === 'screenshot') {
        if (!tableRef.current) throw new Error('table not mounted');
        const blob = await toScreenshotBlob(tableRef.current);
        if (blob.size > TG_PHOTO_MAX) throw new Error(`screenshot ${(blob.size/1024/1024).toFixed(1)} MB exceeds Telegram 10 MB photo cap`);
        opts = { kind: 'photo', blob, filename: `${baseName}.png`, chatId: chatId || undefined, caption: caption.slice(0, captionMax) || undefined };
      } else {
        let blob: Blob; let filename: string; let mime: string;
        if (format === 'csv') {
          blob = await fetchCsvBlob(jobId); filename = `${baseName}.csv`; mime = 'text/csv';
        } else if (format === 'xlsx') {
          blob = await toXlsxBlob(preview); filename = `${baseName}.xlsx`;
          mime = 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet';
        } else {
          blob = await toPdfBlob(preview, templateName); filename = `${baseName}.pdf`; mime = 'application/pdf';
        }
        if (blob.size > TG_DOCUMENT_MAX) throw new Error(`file ${(blob.size/1024/1024).toFixed(1)} MB exceeds Telegram 50 MB document cap`);
        opts = { kind: 'document', blob, filename, mime, chatId: chatId || undefined, caption: caption.slice(0, captionMax) || undefined };
      }
      const r = await ReportsAPI.sendTelegram(jobId, opts);
      setMsg({ kind: 'ok', text: `Sent to chat ${r.chat_id}` });
      setTimeout(onClose, 1200);
    } catch (e: any) {
      setMsg({ kind: 'err', text: e?.message ?? 'send failed' });
    } finally {
      setBusy(false);
    }
  };

  const formatLabel: Record<ExportFormat, string> = {
    csv: 'CSV file', xlsx: 'XLSX file', pdf: 'PDF file',
    screenshot: 'Screenshot', text: 'Text summary',
  };

  return (
    <div className="fixed inset-0 z-40 bg-black/30 flex items-center justify-center"
         onMouseDown={onClose}>
      <div className="card w-96 p-5 space-y-3" onMouseDown={e => e.stopPropagation()}>
        <div>
          <div className="text-xs text-ink-500 uppercase tracking-wide">Send to Telegram</div>
          <div className="text-lg font-semibold text-ink-900">{formatLabel[format]}</div>
        </div>

        <div>
          <label className="label">Chat ID (optional)</label>
          <input className="input text-sm" value={chatId} onChange={e => setChatId(e.target.value)}
                 placeholder="leave blank to use the default" />
        </div>

        {format !== 'text' && (
          <div>
            <label className="label">Caption (optional)</label>
            <input className="input text-sm" value={caption} onChange={e => setCaption(e.target.value)}
                   maxLength={captionMax} />
            <div className="text-[10px] text-ink-400 mt-0.5">{caption.length}/{captionMax}</div>
          </div>
        )}

        {format === 'text' && (
          <div className="text-[11px] text-ink-500">
            Sends the template title, date range, total logins, and the first 10 rows as a code-block message
            (truncated to Telegram's 4096-char limit).
          </div>
        )}

        {msg && (
          <div className={msg.kind === 'ok' ? 'text-xs text-emerald-700' : 'text-xs text-red-600 font-mono'}>
            {msg.text}
          </div>
        )}

        <div className="flex justify-end gap-2">
          <button type="button" className="btn-secondary text-sm" onClick={onClose} disabled={busy}>Cancel</button>
          <button type="button" className="btn-primary text-sm" onClick={send} disabled={busy}>
            {busy ? 'Sending…' : 'Send'}
          </button>
        </div>
      </div>
    </div>
  );
}
