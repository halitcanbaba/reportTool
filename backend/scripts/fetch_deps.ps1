# Fetches single-header / amalgamation dependencies into ReportTool/third_party.
# Run from the backend/ directory: pwsh ./scripts/fetch_deps.ps1
$ErrorActionPreference = "Stop"

$dest = Join-Path $PSScriptRoot "..\ReportTool\third_party"
$dest = (Resolve-Path $dest).Path
Write-Host "Fetching deps into $dest"

if (-not (Test-Path "$dest\httplib.h")) {
    Write-Host " - httplib.h"
    Invoke-WebRequest `
        -Uri "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.5/httplib.h" `
        -OutFile "$dest\httplib.h"
}

if (-not (Test-Path "$dest\json.hpp")) {
    Write-Host " - json.hpp"
    Invoke-WebRequest `
        -Uri "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
        -OutFile "$dest\json.hpp"
}

if (-not (Test-Path "$dest\sqlite3.c")) {
    Write-Host " - sqlite3 amalgamation"
    $zip = Join-Path $env:TEMP "sqlite-amalgamation.zip"
    $unzip = Join-Path $env:TEMP "sqlite-amalgamation-unzip"
    Invoke-WebRequest -Uri "https://sqlite.org/2024/sqlite-amalgamation-3460100.zip" -OutFile $zip
    if (Test-Path $unzip) { Remove-Item $unzip -Recurse -Force }
    Expand-Archive -Path $zip -DestinationPath $unzip -Force
    $src = Get-ChildItem -Path $unzip -Recurse -Filter "sqlite3.c" | Select-Object -First 1
    $dir = Split-Path $src.FullName
    Copy-Item (Join-Path $dir "sqlite3.h") $dest -Force
    Copy-Item (Join-Path $dir "sqlite3.c") $dest -Force
    Remove-Item $zip -Force
    Remove-Item $unzip -Recurse -Force
}

Write-Host "All deps present in $dest"
Get-ChildItem $dest | Format-Table Name, Length
