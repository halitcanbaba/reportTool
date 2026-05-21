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
