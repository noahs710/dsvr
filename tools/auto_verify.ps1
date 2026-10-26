#requires -Version 7.0
# DSVR offline auto-verify: configure + build + test + doctor + latest package.
# Safe: never installs, never launches the game, never touches the game folder.
# On a fully green run this always refreshes dist\DSVR-latest for testing.
# Formal immutable releases additionally need: -Package -Version <new-version>
param([switch]$Package, [string]$Version, [switch]$NoLatest)
$ErrorActionPreference = "Stop"
if (($Package -and [string]::IsNullOrWhiteSpace($Version)) -or
    (!$Package -and ![string]::IsNullOrWhiteSpace($Version))) {
  throw "Use -Package and an explicit new -Version together for a formal release."
}
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $root
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DDSVR_ENABLE_LAB=OFF -DDSVR_BUILD_RESHADE_ADDON=ON
if ($LASTEXITCODE -ne 0) { exit 1 }
cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { exit 1 }
ctest --test-dir build -C Release --output-on-failure 2>&1 | Tee-Object -FilePath build\latest-ctest.txt
if ($LASTEXITCODE -ne 0) { exit 1 }
& .\build\src\setup\Release\DSVR-Setup.exe --doctor
if ($LASTEXITCODE -ne 0) { exit 1 }
if (!$NoLatest) {
  # Mod version label for the dev package (single source: src/common/version.h).
  $label = "dev"
  $vh = Get-Content src\common\version.h -Raw
  if ($vh -match 'kModVersion\[\]\s*=\s*"([^"]+)"') { $label = $Matches[1] }
  python -B tools\package.py --build build --out dist --version $label --latest --reshade-addon --ctest-log build\latest-ctest.txt --doctor pass
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
if ($Package) {
  python -B tools\package.py --build build --out dist --version $Version --reshade-addon
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
