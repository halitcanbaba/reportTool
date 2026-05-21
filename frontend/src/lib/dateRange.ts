//--- Preset date ranges used by every date-picking form in the app.
//--- The MT5 broker uses UTC trading-day boundaries; all math is broker UTC.

const TZ_OFFSET = 0;

function ymdAt(secs: number): string {
  return new Date((secs + TZ_OFFSET) * 1000).toISOString().slice(0, 10);
}
function nowSec(): number { return Math.floor(Date.now() / 1000); }

//--- Broker UTC day-of-week (0=Sun..6=Sat) for now.
function dowLocal(): number {
  return new Date((nowSec() + TZ_OFFSET) * 1000).getUTCDay();
}

export type DatePresetKey =
  | 'today' | 'yesterday'
  | 'last_7'      | 'last_7_to_date'
  | 'last_30'     | 'last_30_to_date'
  | 'this_week'   | 'this_week_to_date'  | 'last_week'
  | 'this_month'  | 'this_month_to_date' | 'last_month';

export type DateRange = { from: string; to: string };

//--- The "_to_date" variants include today; the base variants stop at
//--- yesterday so equity_end-based formulas see a sealed daily snapshot.
//--- See Scheduler.cpp::ResolveRelative — same naming on the backend.
export const PRESETS: { key: DatePresetKey; label: string }[] = [
  { key: 'today',              label: 'Today' },
  { key: 'yesterday',          label: 'Yesterday' },
  { key: 'last_7',             label: 'Last 7 days' },
  { key: 'last_7_to_date',     label: 'Last 7 days (incl. today)' },
  { key: 'last_30',            label: 'Last 30 days' },
  { key: 'last_30_to_date',    label: 'Last 30 days (incl. today)' },
  { key: 'this_week',          label: 'This week' },
  { key: 'this_week_to_date',  label: 'This week (incl. today)' },
  { key: 'last_week',          label: 'Last week' },
  { key: 'this_month',         label: 'This month' },
  { key: 'this_month_to_date', label: 'This month (incl. today)' },
  { key: 'last_month',         label: 'Last month' },
];

export function resolvePreset(key: DatePresetKey): DateRange {
  const now = nowSec();
  switch (key) {
    case 'today':            return { from: ymdAt(now), to: ymdAt(now) };
    case 'yesterday':        { const t = ymdAt(now - 86400); return { from: t, to: t }; }
    case 'last_7':           return { from: ymdAt(now - 7  * 86400), to: ymdAt(now - 86400) };
    case 'last_7_to_date':   return { from: ymdAt(now - 6  * 86400), to: ymdAt(now) };
    case 'last_30':          return { from: ymdAt(now - 30 * 86400), to: ymdAt(now - 86400) };
    case 'last_30_to_date':  return { from: ymdAt(now - 29 * 86400), to: ymdAt(now) };
    case 'this_week': {
      //--- Sealed days only: `to` = yesterday. equity_end on today would
      //--- silently fall back to yesterday's snapshot.
      const dow  = dowLocal();
      const back = dow === 0 ? 6 : dow - 1;   // ISO: Mon..Sun
      return { from: ymdAt(now - back * 86400), to: ymdAt(now - 86400) };
    }
    case 'this_week_to_date': {
      const dow  = dowLocal();
      const back = dow === 0 ? 6 : dow - 1;
      return { from: ymdAt(now - back * 86400), to: ymdAt(now) };
    }
    case 'last_week': {
      const dow  = dowLocal();
      const back = dow === 0 ? 6 : dow - 1;
      return { from: ymdAt(now - (back + 7) * 86400), to: ymdAt(now - (back + 1) * 86400) };
    }
    case 'this_month': {
      //--- Sealed days only: `to` = yesterday (same rationale as this_week).
      const d = new Date((now + TZ_OFFSET) * 1000);
      const y = d.getUTCFullYear(), m = d.getUTCMonth();
      const first = `${y}-${String(m + 1).padStart(2, '0')}-01`;
      return { from: first, to: ymdAt(now - 86400) };
    }
    case 'this_month_to_date': {
      const d = new Date((now + TZ_OFFSET) * 1000);
      const y = d.getUTCFullYear(), m = d.getUTCMonth();
      const first = `${y}-${String(m + 1).padStart(2, '0')}-01`;
      return { from: first, to: ymdAt(now) };
    }
    case 'last_month': {
      const d = new Date((now + TZ_OFFSET) * 1000);
      const y = d.getUTCFullYear(), m = d.getUTCMonth();
      const prevY = m === 0 ? y - 1 : y;
      const prevM = m === 0 ? 11    : m - 1;     // 0-based
      const firstPrev   = `${prevY}-${String(prevM + 1).padStart(2, '0')}-01`;
      const firstOfCur  = Date.UTC(y, m, 1);
      const lastPrev    = new Date(firstOfCur - 86400000).toISOString().slice(0, 10);
      return { from: firstPrev, to: lastPrev };
    }
  }
}

//--- Quick-fill helpers used by the custom-date editor. All ranges are
//--- inclusive ("31 days" for Mar 1 → Mar 31) and broker-UTC.

//--- 28/29/30/31. Handles leap years correctly. month is 1..12.
export function lastDayOfMonth(year: number, month: number): number {
  return new Date(Date.UTC(year, month, 0)).getUTCDate();
}

//--- Parse YYYY-MM-DD → {y,m,d} (m is 1..12). Returns null on bad input.
export function parseYmd(s: string): { y: number; m: number; d: number } | null {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(s);
  if (!match) return null;
  const y = +match[1], m = +match[2], d = +match[3];
  if (m < 1 || m > 12 || d < 1 || d > 31) return null;
  return { y, m, d };
}

//--- Inclusive day count. Returns 0 for invalid / inverted ranges.
export function daysBetween(from: string, to: string): number {
  const a = parseYmd(from), b = parseYmd(to);
  if (!a || !b) return 0;
  const t1 = Date.UTC(a.y, a.m - 1, a.d);
  const t2 = Date.UTC(b.y, b.m - 1, b.d);
  if (t2 < t1) return 0;
  return Math.floor((t2 - t1) / 86400000) + 1;
}

//--- "2026-03" → { from: "2026-03-01", to: "2026-03-31" }. Returns null
//--- if the input is not a YYYY-MM string (e.g. user cleared the picker).
export function monthRange(yyyymm: string): DateRange | null {
  const match = /^(\d{4})-(\d{2})$/.exec(yyyymm);
  if (!match) return null;
  const y = +match[1], m = +match[2];
  if (m < 1 || m > 12) return null;
  const last = lastDayOfMonth(y, m);
  const mm = String(m).padStart(2, '0');
  return { from: `${y}-${mm}-01`, to: `${y}-${mm}-${String(last).padStart(2, '0')}` };
}

//--- (year, 1..4) → { from: "YYYY-Q1Start", to: "YYYY-Q1End" }
export function quarterRange(year: number, q: 1 | 2 | 3 | 4): DateRange {
  const startMonth = (q - 1) * 3 + 1;             // 1, 4, 7, 10
  const endMonth   = startMonth + 2;              // 3, 6, 9, 12
  const endDay     = lastDayOfMonth(year, endMonth);
  const mm1 = String(startMonth).padStart(2, '0');
  const mm2 = String(endMonth).padStart(2, '0');
  return { from: `${year}-${mm1}-01`, to: `${year}-${mm2}-${String(endDay).padStart(2, '0')}` };
}

//--- year → Jan 1 → Dec 31.
export function yearRange(year: number): DateRange {
  return { from: `${year}-01-01`, to: `${year}-12-31` };
}

//--- Pick the (fromKey, toKey) of a 2-param list — preferring canonical names.
export function canonicalRangeKeys(params: string[]): [string, string] | null {
  if (params.length !== 2) return null;
  const fi = params.indexOf('date_from');
  const ti = params.indexOf('date_to');
  if (fi !== -1 && ti !== -1) return [params[fi], params[ti]];
  return [params[0], params[1]];
}

//--- Find which preset (if any) currently matches the given (from,to) pair.
export function detectPreset(from: string, to: string): DatePresetKey | null {
  if (!from || !to) return null;
  for (const p of PRESETS) {
    const r = resolvePreset(p.key);
    if (r.from === from && r.to === to) return p.key;
  }
  return null;
}
