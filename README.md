# MT5 ReportTool

Web-based **template-driven** reporting tool for MetaTrader 5 Manager API.

You design report templates in the browser by composing formulas over MT5 fields
(EOD equity, deal aggregations, open positions, etc.), then run each template
against any manager / date range / saved account filter — over and over.

## Architecture

| Layer    | Tech | Where |
|----------|------|-------|
| Backend  | C++17 (cpp-httplib + nlohmann/json + sqlite3 + MT5 Manager API) | Windows host (`localhost:5151`) |
| Storage  | SQLite WAL + AES-256-GCM (BCrypt) for manager passwords         | next to backend exe |
| Frontend | React + Vite + Tailwind                                          | Docker container (`localhost:8080`) |

The MT5 Manager API ships only as a Windows DLL, so the backend cannot run in a
Linux container. The frontend container reaches the Windows backend via
`host.docker.internal:5151`.

## Concepts

- **Manager** — an MT5 manager account: connection + base group/login filter + regex buckets for deposit/withdrawal/writeoff/adjustment classification.
- **Account Filter** — a reusable preset of `group_masks`, `group_regex`, `login_min/max`. Can be bound to one manager or generic.
- **Report Template** — design-time definition of a per-account report: ordered columns (identifier or formula), a sort spec, default Top N, and named **date params** (e.g. `date_from`, `date_to`).
- **Formula** — an AST built in the visual designer from primitives:
  - identifier text (login, group, …)
  - per-account scalars (user_balance, acc_equity, …)
  - daily snapshot start/end (`equity_start(date_from)`, `equity_end(date_to)`, …)
  - range aggregations (`sum_deposit(date_from, date_to)`, `sum_closed_pl`, `sum_daily_balance`, …)
  - open position / open order aggregations (`position_count`, `order_open_volume_initial_sum`, …)
  - order history aggregations (`count_orders_filled(date_from, date_to)`, …)
  - binary operators `+ − × ÷` and numeric literals.
- **Run** — fill in date param values, optionally pick an account filter or override fields, and submit. Each run is a persisted Job with its CSV output and JSON preview.

## Field surface (single source of truth)

The backend exposes the field catalog at `GET /api/reports/fields`. Categories:

| ID | Category                       | Source                        | Arity |
|----|--------------------------------|-------------------------------|-------|
| A  | Identity                       | IMTUser                       | 0     |
| B  | User Static Numeric            | IMTUser                       | 0     |
| C  | Live Account Snapshot          | IMTAccount                    | 0     |
| D  | Daily Snapshot Start/End       | IMTDaily                      | 1     |
| E  | Daily Δ Range Sums             | IMTDaily                      | 2     |
| F  | Deal Aggregations              | IMTDeal + Classifier          | 2     |
| G  | Open Positions                 | IMTPosition                   | 0     |
| H  | Open Orders                    | IMTOrder (open)               | 0     |
| I  | Order History                  | IMTOrder (history)            | 2     |

Each field is annotated with `source` so the engine **lazy-fetches only the
sources a template's AST references** — unused data is never pulled.

`*_start(D)` returns the latest daily snapshot ≤ D − 1 day; `*_end(D)` returns
the latest snapshot ≤ D. This mirrors the original Top Winner boundary math.

## Build & run — backend (Windows)

Prerequisites: Visual Studio 2022 (v143 toolset), MT5 SDK installed at
`C:\MetaTrader5SDK\`.

```powershell
cd backend
pwsh ./scripts/fetch_deps.ps1                          # one-time: pulls httplib, json, sqlite3
msbuild ReportTool.sln /p:Configuration=Release /p:Platform=x64 /m

$env:REPORTTOOL_MASTER_KEY = (-join ((1..32) | ForEach-Object { '{0:x2}' -f (Get-Random -Max 256) }))
echo $env:REPORTTOOL_MASTER_KEY     # save this somewhere safe!

cd bin\Release
copy ..\..\config\server.json .\config\server.json
.\ReportTool.exe
```

Backend listens on `0.0.0.0:5151`. Tail `data/run.log` for progress.
On first start, schema is created at v2 (or migrated from v1; v1 job history is
dropped since legacy reports are removed).

## Build & run — frontend (Docker)

```bash
cd frontend
docker compose up -d --build
# UI -> http://localhost:8080
```

If frontend and backend live on different machines, set `VITE_API_BASE` in
`.env` or as a build-arg.

## API surface

| Method | Path | Notes |
|--------|------|-------|
| GET    | `/health` | — |
| GET    | `/api/managers` | list |
| GET/POST/PATCH/DELETE | `/api/managers[/:id]` | CRUD |
| POST   | `/api/managers/:id/test` | live connect probe |
| GET    | `/api/account-filters` | list |
| GET/POST/PATCH/DELETE | `/api/account-filters[/:id]` | CRUD |
| GET    | `/api/reports/fields` | catalog of fields (with metadata) |
| GET    | `/api/templates` | list |
| GET/POST/PATCH/DELETE | `/api/templates[/:id]` | CRUD |
| POST   | `/api/templates/validate` | AST validation (no save) |
| POST   | `/api/reports/run` | `{ template_id, manager_id, account_filter_id?, dates, top_n?, account_filter_override? }` |
| GET    | `/api/reports/jobs?limit=` | list |
| GET/DELETE | `/api/reports/jobs/:id` | single / delete |
| GET    | `/api/reports/jobs/:id/download.csv` | UTF-8 BOM CSV |

## Verification (end-to-end)

1. Backend Release built and started; `data/run.log` shows `schema_version → 2`.
2. Frontend: `docker compose up -d --build`, open `http://localhost:8080`.
3. **Managers → New** — add an MT5 manager.
4. **Account Filters → New** — e.g. *Indonesia Live* with `real\Indonesia\*`.
5. **Templates → New** — design `Top Winner — Net Trading`:
   - date params: `date_from, date_to`
   - identifier columns: Login, Group
   - formula columns:
     - *Equity Change* = `equity_end(date_to) − equity_start(date_from)`
     - *Net Deposits* = `sum_deposit(date_from, date_to) + sum_withdrawal(date_from, date_to)`
     - *Trading P/L*  = Equity Change − Net Deposits  (reconstruct AST)
   - sort: *Trading P/L* desc, default top N = 20
6. **Templates → Run** — manager + Indonesia Live + dates → table + CSV.
7. Re-run with a different date range or filter → same template, different cohort.
8. `curl -X POST .../api/reports/top-winner` → 404 (removed; use `/run`).

## Risks / known limits

- **No HTTP auth.** Bound to localhost only.
- **Heavy daily payload.** The engine uses `DailyRequestByLogins` (heavy variant) so all daily fields populate; expect 3–5× network bytes vs `Light`.
- **Currency normalization.** Different accounts may have different currencies; the engine sums in account-native units (no FX conversion in v1).
- **`daily_cache` / `deal_cache`** SQLite tables exist but are not wired in v1.
- **Per-day / aggregate row models** not implemented in v1 (schema reserves space).
- **XLSX export** stretch; only CSV is shipped.
