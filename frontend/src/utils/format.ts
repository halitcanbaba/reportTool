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

//--- The MT5 broker uses UTC trading-day boundaries (daily snapshots at
//--- 23:59:59 UTC), so all dates entered/displayed here are UTC days.
const TZ_OFFSET_SEC = 0;

export function fmtDate(unixSec: number): string {
  if (!unixSec) return '';
  return new Date((unixSec + TZ_OFFSET_SEC) * 1000).toISOString().slice(0, 10);
}

export function fmtDateTime(unixSec: number): string {
  if (!unixSec) return '';
  return new Date((unixSec + TZ_OFFSET_SEC) * 1000).toISOString().replace('T', ' ').slice(0, 19) + ' UTC';
}

//--- Today's date in broker UTC, formatted YYYY-MM-DD. Used for default date inputs.
export function todayLocal(): string {
  const now = Math.floor(Date.now() / 1000);
  return new Date((now + TZ_OFFSET_SEC) * 1000).toISOString().slice(0, 10);
}

//--- YYYY-MM-DD that is N days before today (broker UTC).
export function todayLocalMinus(days: number): string {
  const now = Math.floor(Date.now() / 1000);
  return new Date((now + TZ_OFFSET_SEC - days * 86400) * 1000).toISOString().slice(0, 10);
}
