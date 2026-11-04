#requires -Version 7.0
# DSVR verification preflight: read-only readiness gate for a headset run.
# Never installs, launches, focuses or kills anything. Writes only an
# optional evidence text file under build\verification.
param(
  [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED",
  [string]$OutDir = "",
  [switch]$SkipDoctor
)
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
Set-Location $root
$script:ok = 0; $script:warn = 0; $script:blocked = 0
$script:lines = [System.Collections.Generic.List[string]]::new()
function Say([string]$status, [string]$text) {
  if ($status -eq "OK") { $script:ok++ }
  elseif ($status -eq "WARN") { $script:warn++ }
  else { $script:blocked++ }
  $line = "[{0}] {1}" -f $status, $text
  $script:lines.Add($line)
  Write-Host $line
}
function HashOf([string]$path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [System.IO.File]::OpenRead($path)
  try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace("-", "").ToLower() }
  finally { $stream.Close(); $sha.Dispose() }
}
function IdleSeconds() {
  try {
    Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class DsvrIdle { [StructLayout(LayoutKind.Sequential)] public struct LII { public uint cbSize; public uint dwTime; } [DllImport("user32.dll")] static extern bool GetLastInputInfo(ref LII p); public static double Seconds() { var l = new LII(); l.cbSize = (uint)Marshal.SizeOf(l); GetLastInputInfo(ref l); return (Environment.TickCount - (int)l.dwTime) / 1000.0; } }
"@ -ErrorAction Stop
    return [DsvrIdle]::Seconds()
  } catch { return -1 }
}

Say "OK" "Preflight is read-only: no install, launch, focus or kill will happen."

# Package gate (offline source of truth for what may be installed).
$verifyPath = Join-Path $root "dist\DSVR-latest-verification.json"
if (!(Test-Path $verifyPath)) {
  Say "BLOCKED" "dist\DSVR-latest-verification.json missing. Run tools\auto_verify.ps1 under pwsh first."
  $pkg = $null
} else {
  $pkg = Get-Content $verifyPath -Raw | ConvertFrom-Json
  if ($pkg.ctest.failed -eq 0 -and $pkg.ctest.passed -eq $pkg.ctest.total -and $pkg.doctor -eq "pass") {
    Say "OK" ("Package {0}: CTest {1}/{2}, doctor pass, built {3}." -f $pkg.version_label, $pkg.ctest.passed, $pkg.ctest.total, $pkg.built_utc)
  } else {
    Say "BLOCKED" ("Package gate not green: CTest {0}/{1} failed={2} doctor={3}." -f $pkg.ctest.passed, $pkg.ctest.total, $pkg.ctest.failed, $pkg.doctor)
  }
}

# Built setup binary + optional read-only doctor.
$setupExe = Join-Path $root "build\src\setup\Release\DSVR-Setup.exe"
if (!(Test-Path $setupExe)) {
  Say "BLOCKED" "build\src\setup\Release\DSVR-Setup.exe missing. Build Release first."
} elseif ($SkipDoctor) {
  Say "WARN" "Doctor skipped by request; install health unverified this run."
} else {
  $doctor = & $setupExe --doctor 2>&1 | Out-String
  if ($doctor -match "PASS: pinned build supported") { Say "OK" "Doctor: pinned game build supported." }
  else { Say "BLOCKED" "Doctor did not confirm the pinned game build. Keep the full doctor output with the run evidence." }
  if ($doctor -match "Game is RUNNING") { Say "WARN" "Doctor reports the game running; close it before any install step." }
}

# Processes and user activity. Driving while the user is active is forbidden.
$gameProc = Get-Process -Name "DarkSoulsRemastered" -ErrorAction SilentlyContinue
if ($gameProc) { Say "WARN" ("Game is running (PID {0}). Do not install over it and do not send it input without the runbook route." -f $gameProc.Id) }
else { Say "OK" "Game process is stopped; install and launch steps may proceed in order." }
$idle = IdleSeconds
if ($idle -ge 0 -and $idle -lt 60) { Say "WARN" ("Desktop idle is {0:N0}s (<60s): the user appears active. Prepare only; ask before focusing or driving the game." -f $idle) }
elseif ($idle -ge 60) { Say "OK" ("Desktop idle is {0:N0}s." -f $idle) }
if (Get-Process -Name "steam" -ErrorAction SilentlyContinue) { Say "OK" "Steam client is running." } else { Say "WARN" "Steam client not detected; launch route needs it." }
if (Get-Process -Name "VirtualDesktop.Streamer" -ErrorAction SilentlyContinue) { Say "OK" "Virtual Desktop Streamer is running (VDXR route available)." } else { Say "WARN" "Virtual Desktop Streamer not detected; VDXR route unverified. Qualify SteamVR separately." }

# Active OpenXR runtime must match the route being qualified.
$activeRuntime = $null
try { $activeRuntime = (Get-ItemProperty "HKLM:\SOFTWARE\Khronos\OpenXR\1" -ErrorAction Stop).ActiveRuntime } catch {}
if ($activeRuntime -and (Test-Path $activeRuntime)) {
  if ($activeRuntime -match "virtualdesktop") { Say "OK" "Active OpenXR runtime is VDXR: $activeRuntime" }
  else { Say "WARN" "Active OpenXR runtime is $activeRuntime (not VDXR). Record the route; do not compare its FPS with VDXR." }
} else { Say "BLOCKED" "No readable active OpenXR runtime. Set the intended runtime before a headset run." }

# Offline safety: Steam Offline plus the game's own Offline Mode (no firewall rules).
# Offline Mode makes no online connections until the player turns it back on in
# the game; confirm it on the title screen before every run.
Say "WARN" "Offline Mode is operator-verified: Steam Offline plus Dark Souls Offline Mode on the title screen; see docs\TESTING.md."
$saveRoot = Join-Path $HOME "Documents\NBGI\DARK SOULS REMASTERED"
$saves = @(Get-ChildItem $saveRoot -Recurse -Filter "DRAKS0005.sl2" -ErrorAction SilentlyContinue)
if ($saves.Count -gt 0) { Say "OK" ("Found {0} Documents save file(s); hash them into the run evidence before and after the run." -f $saves.Count) }
else { Say "WARN" "No Documents DRAKS0005.sl2 found; confirm the real known-folder destination before any disposable-save step." }
$backupRoot = Join-Path $HOME ".dsvr-backups"
$latestBackup = Get-ChildItem $backupRoot -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latestBackup -and $latestBackup.LastWriteTime -gt (Get-Date).AddDays(-1)) { Say "OK" ("Latest save backup is fresh: {0} ({1})." -f $latestBackup.Name, $latestBackup.LastWriteTime) }
elseif ($latestBackup) { Say "WARN" ("Latest save backup is {0} ({1}), older than 24h. Take a fresh backup before installing or launching." -f $latestBackup.Name, $latestBackup.LastWriteTime) }
else { Say "WARN" "No backup under $backupRoot; back up both save roots before any run." }

# Installed build must be the packaged build, with the intended test profile.
if ($pkg) {
  foreach ($name in @("dinput8.dll", "DSVR.dll")) {
    $installed = Join-Path $GameDir $name
    if (!(Test-Path $installed)) { Say "BLOCKED" "$name is not installed in the game folder." }
    elseif ((HashOf $installed) -eq $pkg.binaries_sha256.$name) { Say "OK" "Installed $name matches the packaged build." }
    else { Say "BLOCKED" "Installed $name is STALE (does not match the packaged build). With the game closed, install the latest package before verifying; headset results on the stale build prove nothing about current source." }
  }
}
$tomlPath = Join-Path $GameDir "DSVR.toml"
if (Test-Path $tomlPath) {
  $toml = Get-Content $tomlPath -Raw
  if ($toml -match "(?im)^\s*(frame_limit_fps|auto_frame_limit_fps)\s*=") {
    Say "INFO" "Legacy DSVR rate preferences are ignored; DSR-FPS-Unlock selects headset/monitor refresh."
    } else {
      Say "OK" "No static DSVR frame cap; DSR-FPS-Unlock owns rate limiting."
  }
  if ($toml -notmatch "forearm_only") { Say "WARN" "Installed DSVR.toml predates forearm_only/hide_ui_in_vr keys. Saving settings from the new Home menu (or a fresh install) refreshes serialization; do not hand-edit offsets." }
  if ($toml -match "arm_ik_enabled\s*=\s*true" -and $toml -match "arm_ik_log_only\s*=\s*false") { Say "WARN" "Installed profile has live arm IK enabled (not log-only). Keep it only for runs whose route explicitly tests native arms." }
} else { Say "WARN" "No installed DSVR.toml; the first run will use source defaults, which are not the installed test profile." }

# Last-run rate evidence is context, never a pass by itself.
$logPath = Join-Path $GameDir "DSVR.log"
if (Test-Path $logPath) {
  $rateLines = @(Select-String -Path $logPath -Pattern "RateEvidence" | Select-Object -Last 1)
  if ($rateLines.Count) { Say "OK" ("Last log rate line: {0}" -f $rateLines[0].Line.Trim()) }
  else { Say "WARN" "DSVR.log has no RateEvidence line; the next run must produce one per 5s window." }
} else { Say "WARN" "No DSVR.log in the game folder yet." }

Write-Host ("Summary: OK={0} WARN={1} BLOCKED={2}" -f $script:ok, $script:warn, $script:blocked)
$script:lines.Add(("Summary: OK={0} WARN={1} BLOCKED={2}" -f $script:ok, $script:warn, $script:blocked))
if ($script:blocked -gt 0) { $script:lines.Add("HEADSET RUN: BLOCKED - resolve BLOCKED lines first."); Write-Host "HEADSET RUN: BLOCKED - resolve BLOCKED lines first." } else { $script:lines.Add("HEADSET RUN: READY TO START THE RUNBOOK (WARN lines still apply)."); Write-Host "HEADSET RUN: READY TO START THE RUNBOOK (WARN lines still apply)." }
if ($OutDir) {
  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
  $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $evidence = Join-Path $OutDir "preflight-$stamp.txt"
  $script:lines | Set-Content -Path $evidence -Encoding UTF8
  Write-Host "Evidence: $evidence"
}
if ($script:blocked -gt 0) { exit 1 } else { exit 0 }
