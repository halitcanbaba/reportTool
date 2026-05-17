//+------------------------------------------------------------------+
//| reportExport.ts — generate XLSX / PDF / screenshot blobs from a   |
//| job's preview JSON.  All heavy libs are loaded via dynamic        |
//| import() so the initial bundle stays small.                       |
//+------------------------------------------------------------------+
import type { ResultPreview } from '../types';
import { fmtMoney, fmtPct, fmtInt, fmtDate } from '../utils/format';
import { ReportsAPI } from '../api/reports';

//--- Format one preview cell the same way the on-screen table does, so the
//--- exported file matches what the user sees.
function fmtCell(v: number | string | null, fmt: string): string {
  if (v == null) return '';
  if (typeof v === 'string') return v;
  switch (fmt) {
    case 'money': return fmtMoney(v);
    case 'pct':   return fmtPct(v);
    case 'int':   return fmtInt(v);
    case 'date':  return fmtDate(v);
    default:      return String(v);
  }
}

//--- Sanitize a string so it's filesystem-safe for use as a download name.
export function safeBaseName(s: string): string {
  return s.replace(/[^A-Za-z0-9_\-]+/g, '_').replace(/^_+|_+$/g, '') || 'report';
}

//--- XLSX -----------------------------------------------------------
export async function toXlsxBlob(preview: ResultPreview, sheetName = 'Report'): Promise<Blob> {
  const XLSX = await import('xlsx');
  const headers = preview.columns.map(c => c.label);
  //--- Keep numeric cells numeric so Excel formats them, but render dates as
  //--- ISO strings (mirrors fmtDate). Text columns pass through.
  const aoa: (string | number)[][] = [headers];
  for (const row of preview.rows) {
    const out: (string | number)[] = [];
    preview.columns.forEach((c, k) => {
      const v = row[k];
      if (v == null) { out.push(''); return; }
      if (c.format === 'text') { out.push(String(v)); return; }
      if (c.format === 'date') { out.push(typeof v === 'number' ? fmtDate(v) : String(v)); return; }
      out.push(typeof v === 'number' ? v : Number(v) || 0);
    });
    aoa.push(out);
  }
  const ws = XLSX.utils.aoa_to_sheet(aoa);
  const wb = XLSX.utils.book_new();
  XLSX.utils.book_append_sheet(wb, ws, sheetName.slice(0, 31)); // Excel: 31-char limit
  const out = XLSX.write(wb, { type: 'array', bookType: 'xlsx' });
  return new Blob([out], { type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' });
}

//--- PDF ------------------------------------------------------------
export async function toPdfBlob(preview: ResultPreview, title: string): Promise<Blob> {
  const [{ default: jsPDF }, autoTableMod] = await Promise.all([
    import('jspdf'),
    import('jspdf-autotable'),
  ]);
  const autoTable = (autoTableMod as { default: (doc: unknown, opts: unknown) => void }).default;
  //--- Landscape when wide; saves the table from squashing to unreadable.
  const landscape = preview.columns.length >= 6;
  const doc = new jsPDF({ orientation: landscape ? 'landscape' : 'portrait', unit: 'pt', format: 'a4' });

  doc.setFontSize(13);
  doc.text(title, 40, 32);

  const head = [preview.columns.map(c => c.label)];
  const body = preview.rows.map(row => preview.columns.map((c, k) => fmtCell(row[k] ?? null, c.format)));

  autoTable(doc, {
    head,
    body,
    startY: 48,
    styles: { fontSize: 8, cellPadding: 3, overflow: 'linebreak' },
    headStyles: { fillColor: [38, 41, 50], textColor: 255 }, // ink-800 → matches app palette
    alternateRowStyles: { fillColor: [246, 247, 249] },       // ink-50
    margin: { left: 24, right: 24, top: 48, bottom: 32 },
    //--- Render the brand footer on every page autoTable draws.
    didDrawPage: () => {
      const d = doc as unknown as {
        internal: { pageSize: { width: number; height: number; getWidth?: () => number; getHeight?: () => number } };
        setFontSize: (n: number) => void;
        setTextColor: (n: number) => void;
        text: (s: string, x: number, y: number, opts?: { align?: string }) => void;
      };
      const w = d.internal.pageSize.getWidth ? d.internal.pageSize.getWidth() : d.internal.pageSize.width;
      const h = d.internal.pageSize.getHeight ? d.internal.pageSize.getHeight() : d.internal.pageSize.height;
      d.setFontSize(8);
      d.setTextColor(140);
      d.text('powered by bitaker.io', w / 2, h - 12, { align: 'center' });
    },
  });

  const ab = doc.output('arraybuffer');
  return new Blob([ab], { type: 'application/pdf' });
}

//--- Screenshot of an element via html2canvas -----------------------
export async function toScreenshotBlob(el: HTMLElement): Promise<Blob> {
  const { default: html2canvas } = await import('html2canvas');
  //--- scale 2 for crisp text on Hi-DPI; logging off; background white so
  //--- transparent body doesn't render as black.
  const canvas = await html2canvas(el, { scale: 2, backgroundColor: '#ffffff', logging: false });
  return await new Promise<Blob>((resolve, reject) => {
    canvas.toBlob(b => b ? resolve(b) : reject(new Error('toBlob returned null')), 'image/png');
  });
}

//--- CSV (round-trip via existing /download.csv endpoint) ----------
export async function fetchCsvBlob(jobId: number): Promise<Blob> {
  const res = await fetch(ReportsAPI.csvUrl(jobId), { credentials: 'include' });
  if (!res.ok) throw new Error(`csv fetch failed: ${res.status}`);
  return await res.blob();
}

//--- Text summary used by the "Send to Telegram → Text" option ------
//--- Compact fixed-width block; truncates with a footer when it would
//--- exceed Telegram's 4096-char message cap.
export function buildTextSummary(
  preview: ResultPreview,
  templateName: string,
  range: { from?: string; to?: string } | null,
  totalLogins: number,
  maxRows = 10,
): string {
  const lines: string[] = [];
  lines.push(templateName);
  if (range?.from) lines.push(`Period: ${range.from}${range.to && range.to !== range.from ? ` → ${range.to}` : ''}`);
  lines.push(`Total logins: ${totalLogins}`);
  lines.push('');

  const cols = preview.columns;
  const rows = preview.rows.slice(0, maxRows);
  const widths = cols.map((c, k) => {
    const headerW = c.label.length;
    const dataW = Math.max(0, ...rows.map(r => fmtCell(r[k] ?? null, c.format).length));
    return Math.min(20, Math.max(headerW, dataW)); // hard cap 20
  });
  const fmt = (vals: string[]) => vals.map((v, i) => v.padEnd(widths[i]).slice(0, widths[i])).join(' | ');
  const sep = widths.map(w => '-'.repeat(w)).join('-+-');

  lines.push('```');
  lines.push(fmt(cols.map(c => c.label)));
  lines.push(sep);
  for (const r of rows) lines.push(fmt(cols.map((c, k) => fmtCell(r[k] ?? null, c.format))));
  lines.push('```');

  if (preview.rows.length > rows.length) {
    lines.push(`… ${preview.rows.length - rows.length} more rows (see attached file)`);
  }

  const out = lines.join('\n');
  return out.length <= 4096 ? out : out.slice(0, 4080) + '\n… (truncated)';
}
