# Vendored single-header dependencies

These files are not committed to git (size + license clutter). Fetch them once
before building the backend.

## httplib.h (cpp-httplib, MIT)

```powershell
Invoke-WebRequest `
  -Uri "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.5/httplib.h" `
  -OutFile "httplib.h"
```

## json.hpp (nlohmann/json, MIT)

```powershell
Invoke-WebRequest `
  -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
  -OutFile "json.hpp"
```

## sqlite3 amalgamation (public domain)

Download `sqlite-amalgamation-3460100.zip` (or newer) from
<https://sqlite.org/download.html> and extract `sqlite3.h` and `sqlite3.c`
into this directory.

```powershell
$zip = "$env:TEMP\sqlite.zip"
Invoke-WebRequest -Uri "https://sqlite.org/2024/sqlite-amalgamation-3460100.zip" -OutFile $zip
Expand-Archive -Path $zip -DestinationPath "$env:TEMP\sqlite_unzip" -Force
Copy-Item "$env:TEMP\sqlite_unzip\sqlite-amalgamation-3460100\sqlite3.h" .
Copy-Item "$env:TEMP\sqlite_unzip\sqlite-amalgamation-3460100\sqlite3.c" .
```

After all three are present, build with:

```powershell
msbuild ..\..\ReportTool.sln /p:Configuration=Release /p:Platform=x64 /m
```
