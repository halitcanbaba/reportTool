export function fmtMoney(v: number, digits = 2): string {
  if (!Number.isFinite(v)) return '';
  const sign = v < 0 ? '-' : '';
  return sign + '$' + Math.abs(v).toLocaleString('en-US', {
    minimumFractionDigits: digits, maximumFractionDigits: digits,
  });
}

export function fmtNumber(v: number, digits = 2): string {
  if (!Number.isFinite(v)) return '';
  return v.toLocaleString('en-US', {
    minimumFractionDigits: digits, maximumFractionDigits: digits,
  });
}

export function fmtInt(v: number): string {
  if (!Number.isFinite(v)) return '';
  return Math.round(v).toLocaleString('en-US');
}

export function fmtPct(v: number, digits = 2): string {
  if (!Number.isFinite(v)) return '';
  return v.toFixed(digits) + '%';
}

export function fmtDate(unixSec: number): string {
  if (!unixSec) return '';
  return new Date(unixSec * 1000).toISOString().slice(0, 10);
}

export function fmtDateTime(unixSec: number): string {
  if (!unixSec) return '';
  return new Date(unixSec * 1000).toISOString().replace('T', ' ').slice(0, 19);
}
