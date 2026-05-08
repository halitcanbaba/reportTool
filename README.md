# MT5 ReportTool

Web-based reporting tool for MetaTrader 5 Manager API.

Generates two reports:

1. **Top Winner** — per-client winners over a date range
   (Login, Deposit, Withdrawal, Net Deposit, Closed PL, Floating PL Change,
   Balance Writeoff, Trade Adjustments, Net Equity, Company PL)
2. **Summary** — monthly KPI block + per-day breakdown
   (Date, Brand, Deposit, Withdrawal, Net Deposit, Closed PnL, Floating PnL Change,
   Negative Equity Change, Today's Total Equity, New Accounts, Company PnL)

## Architecture

| Layer | Tech | Where |
|-------|------|-------|
| Backend | C++17 (cpp-httplib + nlohmann/json + sqlite3 + MT5 Manager API) | Windows host (`localhost:5151`) |
| Storage | SQLite WAL + AES-256-GCM (BCrypt) for manager passwords | next to backend exe |
| Frontend | React + Vite + Tailwind | Docker container (`localhost:8080`) |

The MT5 Manager API ships only as a Windows DLL, so the backend cannot run in a
Linux container. The frontend container reaches the Windows backend via
`host.docker.internal:5151`.

## Repository layout

```
reportTool/
├── backend/                 (Windows C++)
│   ├── ReportTool.sln
│   ├── ReportTool/
│   │   ├── core/            (Logger, TimeUtil, Records, CsvWriter, ThreadPool, Crypto, RegexCache)
│   │   ├── mt5/             (Connection, ConnectionPool, DataLoader)
│   │   ├── db/              (SqliteDb, Schema, Repos)
│   │   ├── reports/         (Classifier, TopWinnerReport, SummaryReport, ReportWriter)
│   │   ├── api/             (HttpServer, ManagerRoutes, ReportRoutes, JobRunner, AppContext)
│   │   ├── third_party/     (httplib.h, json.hpp, sqlite3.{h,c} — fetched, not committed)
│   │   └── main.cpp
│   ├── config/server.json
│   ├── scripts/fetch_deps.ps1
│   └── data/                (created at runtime: sqlite, output/, run.log)
└── frontend/                (Docker)
    ├── Dockerfile
    ├── docker-compose.yml
    ├── nginx.conf
    └── src/                 (React + TypeScript)
```

## Build & run — backend (Windows)

Prerequisites: Visual Studio 2022 (v143 toolset), MT5 SDK installed at
`C:\MetaTrader5SDK\`.

```powershell
cd backend
pwsh ./scripts/fetch_deps.ps1                          # one-time: pulls httplib, json, sqlite3
msbuild ReportTool.sln /p:Configuration=Release /p:Platform=x64 /m

# Generate a master key and set it
$env:REPORTTOOL_MASTER_KEY = (-join ((1..32) | ForEach-Object { '{0:x2}' -f (Get-Random -Max 256) }))
echo $env:REPORTTOOL_MASTER_KEY     # save this somewhere safe!

cd bin\Release
copy ..\..\config\server.json .\config\server.json
.\ReportTool.exe
```

Backend listens on `0.0.0.0:5151`. Tail `data/run.log` for progress.

## Build & run — frontend (Docker)

```bash
cd frontend
docker compose up -d --build
# UI -> http://localhost:8080
# (forwards API calls to host.docker.internal:5151)
```

If the frontend lives on a different machine than the backend, set
`VITE_API_BASE` in `.env` (or as a build-arg) to the backend URL.

## API surface

| Method | Path | Body / Query |
|--------|------|--------------|
| GET    | `/health` | — |
| GET    | `/api/managers` | — |
| GET    | `/api/managers/:id` | — |
| POST   | `/api/managers` | `{ name, brand, region, server, manager_login, password, group_masks[], group_regex?, login_min?, login_max?, active, regex_filters: { deposit[], withdrawal[], writeoff[], adjustment[] } }` |
| PATCH  | `/api/managers/:id` | partial of the above (omit `password` to keep current) |
| DELETE | `/api/managers/:id` | — |
| POST   | `/api/managers/:id/test` | live MT5 connect probe |
| POST   | `/api/reports/top-winner` | `{ manager_id, date_from, date_to, top_n=20 }` |
| POST   | `/api/reports/summary`    | `{ manager_id, month? \| date_from?+date_to? }` |
| GET    | `/api/reports/jobs` | `?limit=50` |
| GET    | `/api/reports/jobs/:id` | — |
| GET    | `/api/reports/jobs/:id/download.csv`  | — |
| GET    | `/api/reports/jobs/:id/download.xlsx` | — (only if XLSX enabled) |
| DELETE | `/api/reports/jobs/:id` | — |

## Computation rules (authoritative)

For each `DealRow d`, mutually-exclusive precedence is
`deposit > withdrawal > writeoff > adjustment`:

- **Deposit**          = Σ `|profit|` where `action == DEAL_BALANCE` and `comment` matches a deposit regex
- **Withdrawal**       = − Σ `|profit|` where `DEAL_BALANCE` and matches withdrawal regex (kept negative)
- **Net Deposit**      = Deposit + Withdrawal
- **Balance Writeoff** = Σ `profit` where `DEAL_BALANCE` and matches writeoff regex
- **Trade Adjustments**= Σ `profit` where (`DEAL_BALANCE` and matches adjustment regex) OR `action == DEAL_CORRECTION`
- **Closed PL**        = Σ `profit` for `action ∈ {DEAL_BUY, DEAL_SELL}`
- **Floating PL Change** = `Daily.profit` at `(date_to − 1d)` − `Daily.profit` at `(date_from − 1d)`
- **Net Equity**       = `Daily.profit_equity` at the latest snapshot in range
- **Company PL**       = − (Closed PL + Floating PL Change + Net Deposit + Balance Writeoff + Trade Adjustments)

Top Winner is sorted by `(Closed PL + Floating PL Change)` descending. Summary's
monthly metrics are simple sums of the daily rows; Equity Change % uses
yesterday's total equity as the denominator.

## Optimization

- 200-login batches × 120-day windows fanned out over a fixed-size thread pool
  (default 8).
- Per-connection mutex serializes SDK calls on a single `IMTManagerAPI*`
  (parallelism still wins on aggregation + wire-pipelining).
- SQLite WAL cache for sealed historical days/deals — re-running an identical
  report is much faster.
- Regex lists compiled once per request.

## Verification (end-to-end)

1. `curl http://localhost:5151/health` → ok.
2. UI → New Manager → fill (Trive Invest / Indonesia / regex lists).
3. Click Test → backend connects, returns user count.
4. Reports → Top Winner → check first row's `Net Deposit = Deposit + Withdrawal`
   and `Company PL = −(Closed PL + Floating PL Change + Net Deposit + Balance Writeoff + Trade Adjustments)`.
5. Reports → Summary → check `Monthly Net Deposit = Σ daily Net Deposit`.
6. Click Download CSV → file opens in Excel without prompts (UTF-8 BOM).
7. Re-run identical report → second run finishes faster (cache hits in
   `data/run.log`).

## Risks / known limits

- **No HTTP auth.** Bound to localhost only. If exposed on LAN, add a bearer token.
- **XLSX is stretch** — only CSV is shipped in v1. Hand-rolled OOXML can be
  added later behind a build flag.
- **SDK thread-safety undocumented** — we serialize per-connection. Escalate to
  multiple connections per manager if profiling shows wire saturation.
- **Today is never sealed** — reports including today always refetch today's
  rows; rerunning an hour later may yield slightly different numbers.
