# MT5 ReportTool — Backend

Native Windows C++ HTTP server using the MT5 Manager API. See `../README.md`
for the architecture overview.

## One-time setup

```powershell
# 1. Fetch single-header deps
pwsh ./scripts/fetch_deps.ps1

# 2. Generate the master encryption key (64 hex chars / 32 bytes)
$key = -join ((1..32) | ForEach-Object { '{0:x2}' -f (Get-Random -Max 256) })
$env:REPORTTOOL_MASTER_KEY = $key
"$key"     # save this — without it, encrypted manager passwords cannot be decrypted

# 3. Build (Release x64)
msbuild ReportTool.sln /p:Configuration=Release /p:Platform=x64 /m
```

## Run

```powershell
cd bin\Release
mkdir config -ea 0
copy ..\..\config\server.json config\server.json
$env:REPORTTOOL_MASTER_KEY = "<your 64-hex key>"
.\ReportTool.exe
```

Listening on `:5151`. Watch `data/run.log`.

## File map

| Module | Files |
|--------|-------|
| HTTP serving | `api/HttpServer`, `api/{Manager,Report,Health}Routes`, `api/JobRunner` |
| MT5 wiring   | `mt5/Connection`, `mt5/ConnectionPool`, `mt5/DataLoader` |
| DB           | `db/SqliteDb`, `db/Schema`, `db/Repos` |
| Reports      | `reports/Classifier`, `reports/TopWinnerReport`, `reports/SummaryReport`, `reports/ReportWriter` |
| Core         | `core/Logger`, `core/TimeUtil`, `core/Records`, `core/CsvWriter`, `core/ThreadPool`, `core/Crypto`, `core/RegexCache` |

## Adapted from analyzer

`Connection`, `Logger`, `TimeUtil`, `CsvWriter`, `Records`, the deal/daily
batched-fetch pattern, and the comment-classification logic are direct ports
or adaptations from `../../analyzer/MT5Analyzer/`.

## Build settings (vcxproj highlights)

- v143 toolset, stdcpp17, Unicode, x64 only.
- `AdditionalIncludeDirectories`: `C:\MetaTrader5SDK\Include;$(ProjectDir)third_party`
- Link: `bcrypt.lib;ws2_32.lib`
- PostBuild: `xcopy /y /d "C:\MetaTrader5SDK\Libs\MT5APIManager64.dll" "$(OutDir)"`
- `third_party\sqlite3.c` compiled with `<PrecompiledHeader>NotUsing</PrecompiledHeader>`.

## Troubleshooting

- *`REPORTTOOL_MASTER_KEY not set`* — the env var must be set every start.
- *`MT5APIManager64.dll missing`* — confirm SDK at `C:\MetaTrader5SDK\Libs\` and
  the post-build `xcopy` ran.
- *`API version mismatch`* — your SDK headers are newer than the runtime DLL or
  vice versa. Update the SDK install.
