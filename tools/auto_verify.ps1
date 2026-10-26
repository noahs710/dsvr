#requires -Version 7.0
# DSVR offline auto-verify: configure + build + test + doctor + latest package.
# Safe: never installs, never launches the game, never touches the game folder.
# On a fully green run this always refreshes dist\DSVR-latest for testing.
# Formal immutable releases additionally need: -Package -Version <new-version>
param([switch]$Package, [string]$Version, [switch]$NoLatest,
  [switch]$SkipDoctor, [switch]$SkipPackage)
$ErrorActionPreference = "Stop"
if (($Package -and [string]::IsNullOrWhiteSpace($Version)) -or
    (!$Package -and ![string]::IsNullOrWhiteSpace($Version))) {
  throw "Use -Package and an explicit new -Version together for a formal release."
}
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $root
# Re-configuring a matching tree costs ~3.6 s and buys nothing: FetchContent
# deps are already populated and options are pinned below. Only configure when
# the cache is missing or disagrees on generator/platform/key options.
$cache = Join-Path $root "build/CMakeCache.txt"
$needConfigure = $true
if (Test-Path -LiteralPath $cache) {
  $text = Get-Content -LiteralPath $cache -Raw
  $needConfigure = -not ($text -match 'CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026' -and
    $text -match 'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64' -and
    $text -match 'DSVR_ENABLE_LAB:BOOL=OFF' -and
    $text -match 'DSVR_BUILD_RESHADE_ADDON:BOOL=ON')
}
if ($needConfigure) {
  cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DDSVR_ENABLE_LAB=OFF -DDSVR_BUILD_RESHADE_ADDON=ON
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
# --parallel without a number uses all logical cores (/MP handles TU-level
# parallelism inside each target); -j/--parallel on the ctest below is safe:
# every temp-output test is PID-unique.
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit 1 }
ctest --test-dir build -C Release --parallel --output-on-failure 2>&1 | Tee-Object -FilePath build\latest-ctest.txt
if ($LASTEXITCODE -ne 0) { exit 1 }
# Code-only turns skip the game-closed/doctor + zip stages: the inner
# ctest loop already proved the tree. Packaging stays the default so a
# green run still refreshes dist/DSVR-latest for testing.
if (!$SkipDoctor) {
  & .\build\src\setup\Release\DSVR-Setup.exe --doctor
  if ($LASTEXITCODE -ne 0) { exit 1 }
}
if (!$NoLatest -and !$SkipPackage) {
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
