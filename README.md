# MT5 ReportTool

Web-based reporting tool for MetaTrader 5 Manager API. Generates two reports:

1. **Top Winner** — per-client winners over a date range (Login, Deposit, Withdrawal, Net Deposit, Closed PL, Floating PL Change, Balance Writeoff, Trade Adjustments, Net Equity, Company PL).
2. **Summary** — monthly figures + per-day breakdown (Brand, Deposit, Withdrawal, Net Deposit, Closed PnL, Floating PnL Change, Negative Equity Change, Today's Total Equity, Number of New Accounts, Company PnL).

## Architecture

- **Backend** (Windows native): C++ HTTP server using `cpp-httplib` + `nlohmann/json`. Connects to MT5 Manager API. SQLite for manager configs, regex filters, job history, and daily/deal cache. Listens on `localhost:5151`.
- **Frontend** (Docker): React + Vite + TypeScript + Tailwind, served by `nginx:alpine`. Reaches the Windows backend via `host.docker.internal:5151`.

## Manager configuration

Each manager record holds: name, brand, region, server, manager_login, encrypted password, group_masks (MT5 wildcard list), optional group_regex post-filter, optional login_min/login_max range, and four regex lists for `DEAL_BALANCE` comment classification — `deposit`, `withdrawal`, `writeoff`, `adjustment`.

## Status

Bootstrap. Implementation plan at: `/home/halit/.claude/plans/c-users-halit-onedrive-documents-mq-mt5-structured-thacker.md`.
