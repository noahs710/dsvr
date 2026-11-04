#requires -Version 7.0
# Bounded combat observer. Synthetic HMD input is an explicit opt-in mode.
# Files/save roots are changed only inside a receipted, reversible transaction.
# Recovered from build/desktop-observer-session.ps1; do not use its old observer
# deployment (duplicate observer_only and regular, non-LAB core).
param(
  [ValidateSet('Check','Launch','Run','Finish','Restore','SelfTest')][string]$Mode='Check',
  [string]$Receipt,
  [string]$CandidateReceipt='build\verification\combat-observability-20261024\candidate-final-receipt.json',
  # Setup is part of the fixed session deadline. The operator-approved longer
  # retry needs 30 minutes; retain the 15-minute default and a finite upper bound.
  [ValidateRange(1,30)][int]$WindowMinutes=15,
  [ValidateRange(1,30)][int]$CloseWaitMinutes=5,
  [ValidateRange(1,120)][int]$FooterWaitSeconds=30,
  [string]$Cases,
  [string]$SeedVault=(Join-Path $env:USERPROFILE '.dsvr-backups\desktop-observer-20261023-140650'),
  [ValidateSet('ObserverOnly','SyntheticHmd','OpenXRSimulator')][string]$InputMode='OpenXRSimulator',
  [string]$HmdLayerDir='build\tools\hmd\Release',
  [ValidateRange(0,1800)][int]$SimulatorSoakSeconds=600,
  [ValidateRange(0,600)][int]$MenuBudgetSeconds=150
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$game='C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED'
$steam='C:\Program Files (x86)\Steam'
$account=1843241039
$backupParent=Join-Path $env:USERPROFILE '.dsvr-backups'
$transactionParent=[IO.Path]::GetFullPath((Join-Path $repo 'build\native-transactions'))
$saveRoots=@((Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'NBGI\DARK SOULS REMASTERED'),
             (Join-Path $env:APPDATA 'DarkSoulsRemastered'))
$installNames=@('dinput8.dll','DSVR.dll','DSVR.toml')
$script:runtimeRoot=$null
$script:steamVrSettingsPath=$null
$script:steamVrOriginal=$null
$script:simulatorRuntimeRoot=$null
$script:simulatorDataDir=$null
$script:simulatorControlProcess=$null
$script:simulatorControlStopPath=$null
$script:simulatorControlReport=$null
$script:simulatorLastMemorySample=[datetime]::MinValue
$script:simulatorLastReceiptSave=[datetime]::MinValue
$script:simulatorRuntimeLock=$null
$script:steamVrRequested=[ordered]@{
  'steamvr.forcedDriver'='null';'steamvr.activateMultipleDrivers'=$false;'driver_null.enable'=$true
  'steamvr.startMonitorFromAppLaunch'=$false;'steamvr.startDashboardFromAppLaunch'=$false
  'steamvr.startOverlayAppsFromDashboard'=$false
}
$script:hmdMutex=$null
function Hash($path) {
  (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function RelPath($root,$full) {
  [IO.Path]::GetRelativePath([IO.Path]::GetFullPath($root),[IO.Path]::GetFullPath($full))
}
function Games { @(Get-Process -Name DarkSoulsRemastered -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited }) }
function RuntimeRootFromActiveRuntime([string]$activeRuntime) {
  if(-not $activeRuntime -or [IO.Path]::GetFileName($activeRuntime) -ine 'steamxr_win64.json') {
    throw 'Active OpenXR runtime is not the expected SteamVR manifest.'
  }
  return [IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($activeRuntime)).TrimEnd('\')
}
function SteamVrProcesses {
  if(-not $script:runtimeRoot) { return @() }
  $prefix=$script:runtimeRoot.TrimEnd('\')+'\'
  @(Get-Process -Name vrserver,vrcompositor,vrmonitor,vrstartup,vrdashboard,vrwebhelper -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) })
}
function ReadJsonSetting($settings,[string]$key) {
  $group,$name=$key.Split('.',2)
  if($settings.Contains($group) -and $settings[$group].Contains($name)) { return @{present=$true;value=$settings[$group][$name]} }
  return @{present=$false;value=$null}
}
function SetJsonSetting($settings,[string]$key,$value) {
  $group,$name=$key.Split('.',2)
  if(-not $settings.Contains($group)) { $settings[$group]=@{} }
  $settings[$group][$name]=$value
}
function WriteJsonSettings([string]$path,$settings) {
  $temporary=$path+'.dsvr-'+[guid]::NewGuid().ToString('N')+'.tmp'
  try {
    [IO.File]::WriteAllText($temporary,($settings|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary,$path,$true)
  } finally { if(Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
}
function RestoreJsonSettings([string]$path,$original,$requested) {
  $current=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
  foreach($key in $requested.Keys) {
    $now=ReadJsonSetting $current $key
    if(-not $now.present -or $now.value -cne $requested[$key]) { throw "Concurrent change to SteamVR setting $key; preserved current settings." }
  }
  foreach($key in $requested.Keys) {
    $before=ReadJsonSetting $original $key
    if($before.present) { SetJsonSetting $current $key $before.value }
    else {
      $group,$name=$key.Split('.',2)
      $current[$group].Remove($name)|Out-Null
      if($current[$group].Count -eq 0 -and -not $original.Contains($group)) { $current.Remove($group)|Out-Null }
    }
  }
  WriteJsonSettings $path $current
  $verified=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
  foreach($key in $requested.Keys) {
    $before=ReadJsonSetting $original $key; $after=ReadJsonSetting $verified $key
    if($before.present -ne $after.present -or $before.value -cne $after.value) { throw "Failed to restore SteamVR setting $key." }
  }
}
function ApplySteamVrSettings {
  if($InputMode -ne 'SyntheticHmd') { return }
  if(@(SteamVrProcesses).Count) { throw 'SteamVR started before its null-HMD settings transaction; refusing to take over the runtime.' }
  $script:steamVrOriginal=Get-Content -LiteralPath $script:steamVrSettingsPath -Raw | ConvertFrom-Json -AsHashtable
  $snapshot=Join-Path $vault 'steamvr-settings-before.json'
  Copy-Item -LiteralPath $script:steamVrSettingsPath -Destination $snapshot
  $r.runtime_settings_path=$script:steamVrSettingsPath
  $r.runtime_settings_snapshot=$snapshot
  $r.runtime_settings_snapshot_sha256=Hash $snapshot
  $r.runtime_settings_requested=$script:steamVrRequested
  if($r.synthetic_layer.settings_baseline_sha256 -and $r.runtime_settings_snapshot_sha256 -ne $r.synthetic_layer.settings_baseline_sha256) {
    $r.runtime_settings_state='preflight_conflict'; SaveReceipt
    throw 'SteamVR settings changed after read-only preflight.'
  }
  $r.runtime_settings_state='pending'
  SaveReceipt
  $current=Get-Content -LiteralPath $script:steamVrSettingsPath -Raw | ConvertFrom-Json -AsHashtable
  foreach($key in $script:steamVrRequested.Keys) { SetJsonSetting $current $key $script:steamVrRequested[$key] }
  WriteJsonSettings $script:steamVrSettingsPath $current
  $r.runtime_settings_state='applied'
  SaveReceipt
}
function RestoreSteamVrSettings {
  if($InputMode -ne 'SyntheticHmd' -or $r.runtime_settings_state -notin @('pending','applied')) { return }
  # The launch path keeps an ordered dictionary, while -Mode Restore reads
  # the receipt as a PSCustomObject and normalizes it before recovery.
  $requested=$script:steamVrRequested
  $original=Get-Content -LiteralPath $r.runtime_settings_snapshot -Raw | ConvertFrom-Json -AsHashtable
  $current=Get-Content -LiteralPath $r.runtime_settings_path -Raw | ConvertFrom-Json -AsHashtable
  $alreadyOriginal=$true
  foreach($key in $requested.Keys) {
    $before=ReadJsonSetting $original $key; $now=ReadJsonSetting $current $key
    if($before.present -ne $now.present -or $before.value -cne $now.value) { $alreadyOriginal=$false }
  }
  if(-not $alreadyOriginal) { RestoreJsonSettings $r.runtime_settings_path $original $requested }
  $r.runtime_settings_state='restored'
  SaveReceipt
}
function Inventory($path) {
  if(-not (Test-Path -LiteralPath $path)) { return }
  $entries=@(Get-Item -LiteralPath $path -Force)+@(Get-ChildItem -LiteralPath $path -Force -Recurse)
  foreach($entry in $entries) {
    if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point refused: $($entry.FullName)" }
    if(-not $entry.PSIsContainer) {
      [pscustomobject]@{relative=(RelPath $path $entry.FullName);sha256=(Hash $entry.FullName)}
    }
  }
}
function AssertInventory($path,$expected) {
  $actual=@(Inventory $path | ForEach-Object { $_.relative+':'+$_.sha256 } | Sort-Object)
  $wanted=@($expected | ForEach-Object { $_.relative+':'+$_.sha256 } | Sort-Object)
  if(($actual -join "`n") -cne ($wanted -join "`n")) { throw "Inventory changed: $path" }
}
function WriteNewReceiptJson($path,$value) {
  $temporary=$path+'.pending-'+[guid]::NewGuid().ToString('N')
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 16))
  $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) }
  finally { $stream.Dispose() }
  # Publish only complete files. Neither destination nor prior versions are replaced.
  [IO.File]::Move($temporary,$path)
}
function QuoteWindowsArgument([string]$value) {
  # CommandLineToArgvW-compatible quoting for Start-Process, which joins its
  # ArgumentList before CreateProcess. Double backslashes only where required.
  $builder=[Text.StringBuilder]::new()
  [void]$builder.Append('"')
  $slashes=0
  foreach($character in $value.ToCharArray()) {
    if($character -eq [char]92) { ++$slashes; continue }
    if($character -eq [char]34) {
      for($i=0;$i -lt (2*$slashes+1);++$i) { [void]$builder.Append([char]92) }
      [void]$builder.Append([char]34); $slashes=0; continue
    }
    for($i=0;$i -lt $slashes;++$i) { [void]$builder.Append([char]92) }
    $slashes=0; [void]$builder.Append($character)
  }
  for($i=0;$i -lt (2*$slashes);++$i) { [void]$builder.Append([char]92) }
  [void]$builder.Append('"')
  return $builder.ToString()
}
function StartDetachedPowerShell($arguments,$stdoutPath,$stderrPath,$workingDirectory) {
  $pwsh=Get-Command pwsh -ErrorAction Stop
  $argumentLine=(@($arguments | ForEach-Object { QuoteWindowsArgument ([string]$_) }) -join ' ')
  return Start-Process -FilePath $pwsh.Source -ArgumentList $argumentLine `
    -WorkingDirectory $workingDirectory -RedirectStandardOutput $stdoutPath `
    -RedirectStandardError $stderrPath -WindowStyle Hidden -PassThru
}
function ReadReceipt {
  $latest=Get-Content -LiteralPath $Receipt -Raw | ConvertFrom-Json
  if($null -eq $latest) { throw 'Empty receipt.' }
  $baseHash=Hash $Receipt; $previous=$baseHash; $revision=0
  $journal=$Receipt+'.journal'
  if(Test-Path -LiteralPath $journal) {
    if((Get-Item -LiteralPath $journal -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse receipt journal refused.' }
    foreach($file in @(Get-ChildItem -LiteralPath $journal -Filter '*.json' -File | Sort-Object Name)) {
      if($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse receipt version refused.' }
      if($file.Name -notmatch '^(\d{8})-([0-9a-f]{64})\.json$') { throw 'Unknown receipt journal entry.' }
      $number=[int]$Matches[1]; $declaredHash=$Matches[2]
      if($number -ne $revision+1 -or (Hash $file.FullName) -ne $declaredHash) { throw 'Receipt version hash/order mismatch.' }
      $entry=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
      if($entry.schema -ne 1 -or $entry.revision -ne $number -or
         $entry.base_sha256 -ne $baseHash -or $entry.previous_sha256 -ne $previous -or
         $null -eq $entry.receipt) { throw 'Receipt journal chain mismatch.' }
      $latest=$entry.receipt; $revision=$number; $previous=$declaredHash
    }
  }
  $script:receiptRevision=$revision; $script:receiptPrevious=$previous
  return $latest
}
function SaveReceipt {
  if(-not (Test-Path -LiteralPath $Receipt)) { WriteNewReceiptJson $Receipt $script:r }
  $null=ReadReceipt
  $journal=$Receipt+'.journal'
  [IO.Directory]::CreateDirectory($journal) | Out-Null
  $revision=$script:receiptRevision+1
  $entry=[ordered]@{schema=1;revision=$revision;base_sha256=(Hash $Receipt);previous_sha256=$script:receiptPrevious;receipt=$script:r}
  $pending=Join-Path $journal ('.pending-'+[guid]::NewGuid().ToString('N'))
  WriteNewReceiptJson $pending $entry
  $digest=Hash $pending
  $destination=Join-Path $journal ('{0:D8}-{1}.json' -f $revision,$digest)
  [IO.File]::Move($pending,$destination)
}
function CopyTree($source,$destination) {
  $inventory=@(Inventory $source)
  foreach($item in $inventory) {
    $target=Join-Path $destination $item.relative
    [IO.Directory]::CreateDirectory((Split-Path $target -Parent)) | Out-Null
    Copy-Item -LiteralPath (Join-Path $source $item.relative) -Destination $target
  }
  AssertInventory $destination $inventory
  return $inventory
}
function StageFile($file,$source) {
  if((Hash $file.path) -ne $file.expectedHash) { throw "Unexpected installed-file change: $($file.path)" }
  $file.expectedHash=Hash $source
  SaveReceipt # intent before mutation; old and expected bytes are both recoverable
  Copy-Item -LiteralPath $source -Destination $file.path
  if((Hash $file.path) -ne $file.expectedHash) { throw 'Candidate copy hash mismatch.' }
}
function AssertFiles($files) {
  foreach($file in $files) {
    if((Hash $file.backup) -ne $file.originalHash) { throw "Backup changed: $($file.backup)" }
    $current=Hash $file.path
    if($current -ne $file.expectedHash -and $current -ne $file.originalHash) {
      throw "Unexpected installed-file change retained: $($file.path)"
    }
  }
}
function RestoreFiles($files) {
  AssertFiles $files
  foreach($file in $files) {
    if((Hash $file.path) -ne $file.originalHash) { Copy-Item -LiteralPath $file.backup -Destination $file.path }
    if((Hash $file.path) -ne $file.originalHash) { throw 'Restore hash mismatch.' }
    $file.expectedHash=$file.originalHash
    SaveReceipt
  }
}
function StageRoots($roots) {
  foreach($root in $roots) {
    AssertInventory $root.original $root.inventory
    if((Test-Path -LiteralPath $root.held) -or (Test-Path -LiteralPath $root.testOutput)) { throw 'Save staging target already exists.' }
    $root.moved=$true; SaveReceipt
    # This must be a directory rename, never a provider copy/merge. Check both
    # sides before seeding: an independently recreated root must not receive a
    # disposable save over newer personal files.
    [IO.Directory]::Move([IO.Path]::GetFullPath($root.original),[IO.Path]::GetFullPath($root.held))
    AssertInventory $root.held $root.inventory
    if(Test-Path -LiteralPath $root.original) { throw "Save root reappeared after rename: $($root.original)" }
    [IO.Directory]::CreateDirectory($root.original) | Out-Null
    AssertInventory $root.original @()
    CopyTree $root.seed $root.original | Out-Null
  }
}
function AssertRoots($roots) {
  foreach($root in $roots) {
    if(Test-Path -LiteralPath $root.held) { AssertInventory $root.held $root.inventory }
    elseif($root.moved) { AssertInventory $root.original $root.inventory }
    if((Test-Path -LiteralPath $root.held) -and (Test-Path -LiteralPath $root.testOutput)) {
      throw 'Refusing to overwrite an existing test-output root.'
    }
  }
}
function RestoreRoots($roots) {
  AssertRoots $roots
  foreach($root in $roots) {
    if(Test-Path -LiteralPath $root.held) {
      if(Test-Path -LiteralPath $root.original) { Move-Item -LiteralPath $root.original -Destination $root.testOutput }
      Move-Item -LiteralPath $root.held -Destination $root.original
    }
    $root.moved=$false; SaveReceipt
    AssertInventory $root.original $root.inventory
  }
}
function ObserverConfig {
  return @'
[loader]
chain_proxy = ""
[vr]
vr_enabled = false
headlook_enabled = false
reshade_host = false
force_disable_motion_blur = false
[controls]
xr_input_enabled = false
hotkeys_enabled = true
log_marker_vk = 0x78
[camera]
hide_player_model = false
hide_second_model = false
hide_head = false
[combat]
motion_attacks = false
haptics = false
arm_ik_enabled = false
arm_ik_log_only = true
combat_test_bench = false
[diagnostics]
observer_only = true
combat_trace_only = true
verbose_logging = true
[safety]
offline_only_ack = true
'@
}
function OpenXRSimulatorConfig {
  return @'
[loader]
chain_proxy = ""
[vr]
vr_enabled = true
# Keep the game window visible for a supervised live run. The no-mirror
# default re-hides it every frame while the XR session is visible, which let a
# hidden game consume the operator's input in the aborted r12 run and makes a
# foreground proof impossible.
desktop_present = true
headlook_enabled = true
reshade_host = false
force_disable_motion_blur = false
gameplay_theater = false
[controls]
xr_input_enabled = true
hotkeys_enabled = false
log_marker_vk = 0x78
[camera]
hide_player_model = false
hide_second_model = false
hide_head = false
[combat]
motion_attacks = true
physical_attacks = true
physical_defence = false
haptics = false
arm_ik_enabled = true
arm_ik_log_only = true
combat_test_bench = false
[diagnostics]
observer_only = false
combat_trace_only = false
verbose_logging = true
[safety]
offline_only_ack = true
'@
}
function OpenXRSimulatorLock {
  $root=[IO.Path]::GetFullPath((Join-Path $repo 'build\research\openxr-simulator-upstream-build'))
  $lockPath=Join-Path $root 'upstream-lock.json'
  if(-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { throw 'Official OpenXR-Simulator lock is missing; run tools/hmd/setup_openxr_simulator.ps1.' }
  $lock=Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
  if($lock.schema -ne 2 -or $lock.source_commit -ne '692a66de570189b29c684f48c6ef306a07d5764f' -or $lock.upstream_unmodified -ne $true) {
    throw 'OpenXR-Simulator source is not the pinned unmodified upstream checkout.'
  }
  foreach($field in @('runtime_manifest','runtime_dll','mcp_server','mcp_python','viewer')) {
    if(-not (Test-Path -LiteralPath $lock.$field -PathType Leaf)) { throw "OpenXR-Simulator $field is missing." }
  }
  foreach($pair in @(@($lock.runtime_manifest,$lock.runtime_manifest_sha256),@($lock.runtime_dll,$lock.runtime_dll_sha256),@($lock.mcp_server,$lock.mcp_server_sha256),@($lock.viewer,$lock.viewer_sha256))) {
    if((Hash $pair[0]) -ne $pair[1]) { throw "OpenXR-Simulator pinned artifact hash mismatch: $($pair[0])" }
  }
  & $lock.mcp_python -B -c 'import mcp, numpy, PIL'
  if($LASTEXITCODE -ne 0) { throw 'Official OpenXR-Simulator MCP dependencies failed their import check.' }
  & $lock.mcp_python -m pip check
  if($LASTEXITCODE -ne 0) { throw 'Official OpenXR-Simulator MCP dependencies are inconsistent.' }
  return $lock
}
function TrackOpenXRSimulatorProcesses {
  if($InputMode -ne 'OpenXRSimulator' -or -not $script:simulatorControlProcess -or -not $r.launch_requested) { return }
  $control=$script:simulatorControlProcess
  try { $control.Refresh() } catch { return }
  if($control.HasExited) { return }
  $controlPath=$control.Path
  $controlStart=$control.StartTime.ToUniversalTime()
  $existing=@($r.simulator_control | Where-Object { [int]$_.id -eq $control.Id })
  if($existing.Count -and ($existing.Count -ne 1 -or $existing[0].path -ine $controlPath -or
     ([datetime]$existing[0].started_utc).Ticks -ne $controlStart.Ticks)) {
    throw 'Simulator MCP controller PID was reused or changed identity.'
  }
  if(-not $existing.Count) {
    $r.simulator_control+=@([pscustomobject]@{id=$control.Id;path=$controlPath;started_utc=$controlStart.ToString('o')})
    SaveReceipt
  }
  $serverScript=[IO.Path]::GetFullPath($r.simulator.mcp_server)
  $children=@(Get-CimInstance Win32_Process | Where-Object {
    $_.ParentProcessId -eq $control.Id -and $_.CommandLine -like "*$serverScript*"
  })
  foreach($child in $children) {
    $id=[int]$child.ProcessId
    $identity=@($r.simulator_server_processes | Where-Object { [int]$_.id -eq $id })
    if($identity.Count -and $identity[0].command_line -cne $child.CommandLine) {
      throw "Simulator MCP server PID $id was reused or its command changed."
    }
    if(-not $identity.Count) {
      $r.simulator_server_processes+=@([pscustomobject]@{id=$id;name=$child.Name;command_line=$child.CommandLine;started_utc=$child.CreationDate.ToUniversalTime().ToString('o')})
      SaveReceipt
    }
  }
}
function SampleOpenXRSimulatorMemory {
  if($InputMode -ne 'OpenXRSimulator' -or -not $script:simulatorControlProcess -or $script:simulatorControlProcess.HasExited) { return }
  $now=[datetime]::UtcNow
  if($script:simulatorLastMemorySample -ne [datetime]::MinValue -and ($now-$script:simulatorLastMemorySample).TotalSeconds -lt 1) { return }
  $script:simulatorLastMemorySample=$now
  TrackGames
  TrackOpenXRSimulatorProcesses
  foreach($identity in @($r.pids)) {
    $process=Get-Process -Id ([int]$identity.id) -ErrorAction SilentlyContinue
    if(-not $process -or $process.Path -ine $identity.path -or $process.StartTime.Ticks -ne ([datetime]$identity.start).Ticks) {
      throw 'Owned DSR process disappeared or changed identity during simulator memory sampling.'
    }
    $private=[math]::Ceiling($process.PrivateMemorySize64/1MB)
    $working=[math]::Ceiling($process.WorkingSet64/1MB)
    Add-Content -LiteralPath $script:simulatorMemoryPath -Value (([datetime]::UtcNow.ToString('o'))+',game,'+$process.Id+','+$private+','+$working)
    $r.memory_samples++
    $key="game:$($process.Id)"
    if(-not $r.memory_peak_private_mb.Contains($key) -or $r.memory_peak_private_mb[$key] -lt $private) { $r.memory_peak_private_mb[$key]=$private }
    if($private -gt 6144) { throw "DSR/OpenXR-Simulator process memory exceeded 6144 MiB: $private MiB." }
    try {
      $modules=@($process.Modules | ForEach-Object { [IO.Path]::GetFullPath($_.FileName) })
      if($modules -contains [IO.Path]::GetFullPath($r.simulator.runtime_dll)) { $r.simulator.runtime_module_loaded=$true }
      if($modules -contains [IO.Path]::GetFullPath($r.synthetic_layer.directory+'\DSVR-HmdLayer.dll')) { $r.simulator.layer_module_loaded=$true }
    } catch { }
  }
  $control=$script:simulatorControlProcess
  $control.Refresh()
  if(-not $control.HasExited) {
    $private=[math]::Ceiling($control.PrivateMemorySize64/1MB); $working=[math]::Ceiling($control.WorkingSet64/1MB)
    Add-Content -LiteralPath $script:simulatorMemoryPath -Value (([datetime]::UtcNow.ToString('o'))+',mcp_driver,'+$control.Id+','+$private+','+$working)
    $r.memory_samples++; $key="mcp_driver:$($control.Id)"
    if(-not $r.memory_peak_private_mb.Contains($key) -or $r.memory_peak_private_mb[$key] -lt $private) { $r.memory_peak_private_mb[$key]=$private }
    if($private -gt 1024) { throw "Simulator MCP driver exceeded 1024 MiB: $private MiB." }
  }
  foreach($identity in @($r.simulator_server_processes)) {
    $id=[int]$identity.id
    $process=Get-Process -Id $id -ErrorAction SilentlyContinue
    if(-not $process) { continue }
    $observed=Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction SilentlyContinue
    if(-not $observed -or $observed.CommandLine -cne $identity.command_line) { throw "MCP server process identity changed for PID $id." }
    $private=[math]::Ceiling($process.PrivateMemorySize64/1MB); $working=[math]::Ceiling($process.WorkingSet64/1MB)
    Add-Content -LiteralPath $script:simulatorMemoryPath -Value (([datetime]::UtcNow.ToString('o'))+',mcp_server,'+$id+','+$private+','+$working)
    $r.memory_samples++; $key="mcp_server:$id"
    if(-not $r.memory_peak_private_mb.Contains($key) -or $r.memory_peak_private_mb[$key] -lt $private) { $r.memory_peak_private_mb[$key]=$private }
    if($private -gt 1024) { throw "Simulator MCP server exceeded 1024 MiB: $private MiB." }
  }
  if($script:simulatorLastReceiptSave -eq [datetime]::MinValue -or ($now-$script:simulatorLastReceiptSave).TotalSeconds -ge 10) {
    $script:simulatorLastReceiptSave=$now; SaveReceipt
  }
}
function StartOpenXRSimulatorControl {
  if($InputMode -ne 'OpenXRSimulator') { return }
  $script:simulatorDataDir=Join-Path $evidence 'simxr-data'
  New-Item -ItemType Directory -Force -Path $script:simulatorDataDir | Out-Null
  $script:simulatorMemoryPath=Join-Path $evidence 'memory.csv'
  [IO.File]::WriteAllText($script:simulatorMemoryPath,"time_utc,role,pid,private_mb,working_set_mb`n",[Text.Encoding]::ASCII)
  $driver=Join-Path $repo 'tools\hmd\simulator_mcp_driver.py'
  $runtimeLock=OpenXRSimulatorLock
  $python=[IO.Path]::GetFullPath($runtimeLock.mcp_python)
  $arguments=@('"'+$driver+'"','--server','"'+$runtimeLock.mcp_server+'"',
    '--data-dir','"'+$script:simulatorDataDir+'"','--output','"'+(Join-Path $evidence 'mcp-run.json')+'"',
    '--soak-seconds',"$SimulatorSoakSeconds",'--menu-seconds',"$MenuBudgetSeconds",'--game-dir','"'+$game+'"','--stop-file','"'+(Join-Path $evidence 'mcp.stop')+'"',
    '--finish-file','"'+$script:FinishPath+'"','--done-file','"'+(Join-Path $evidence 'mcp.done')+'"') -join ' '
  $script:simulatorControlProcess=Start-Process -FilePath $python -ArgumentList $arguments -WorkingDirectory (Split-Path $driver -Parent) -PassThru `
    -RedirectStandardOutput (Join-Path $evidence 'mcp.stdout.log') -RedirectStandardError (Join-Path $evidence 'mcp.stderr.log')
  $r.simulator_control_id=$script:simulatorControlProcess.Id
  $r.simulator_control_path=$script:simulatorControlProcess.Path
  $r.simulator_control_started_utc=$script:simulatorControlProcess.StartTime.ToUniversalTime().ToString('o')
  SaveReceipt
}
function WaitForOpenXRSimulatorControl {
  if($InputMode -ne 'OpenXRSimulator' -or -not $script:simulatorControlProcess) { return }
  $limit=[datetime]::UtcNow.AddSeconds(120)
  while(-not $script:simulatorControlProcess.HasExited -and [datetime]::UtcNow -lt $limit) {
    TrackGames; SampleOpenXRSimulatorMemory
    Start-Sleep -Milliseconds 200
  }
  if(-not $script:simulatorControlProcess.HasExited) { throw 'Simulator MCP controller did not complete within 120 seconds after the declared soak.' }
  $script:simulatorControlProcess.WaitForExit()
  $r.simulator_control_exit=$script:simulatorControlProcess.ExitCode
  $mcpPath=Join-Path $evidence 'mcp-run.json'
  if(-not (Test-Path -LiteralPath $mcpPath -PathType Leaf)) { throw 'Simulator MCP driver exited without mcp-run.json.' }
  $r.simulator_mcp_report=$mcpPath
  $r.simulator_mcp_verdict=(Get-Content -LiteralPath $mcpPath -Raw | ConvertFrom-Json).verdict
  SaveReceipt
  if($r.simulator_control_exit -ne 0 -or $r.simulator_mcp_verdict -ne 'PASS') { throw "Simulator MCP run failed (exit=$($r.simulator_control_exit), verdict=$($r.simulator_mcp_verdict))." }
}
function StopOpenXRSimulatorControl {
  if($InputMode -ne 'OpenXRSimulator') { return }
  if($script:simulatorControlProcess -and -not $script:simulatorControlProcess.HasExited) {
    $stopPath=Join-Path $evidence 'mcp.stop'
    if(-not (Test-Path -LiteralPath $stopPath)) { [IO.File]::WriteAllText($stopPath,'supervisor-cleanup',[Text.Encoding]::ASCII) }
    if(-not $script:simulatorControlProcess.WaitForExit(20000)) {
      $script:simulatorControlProcess.Refresh()
      if(-not $script:simulatorControlProcess.HasExited) {
        $r.simulator_control_forced=$true
        Stop-Process -Id $script:simulatorControlProcess.Id -Force
      }
    }
    if($script:simulatorControlProcess.HasExited) { $r.simulator_control_exit=$script:simulatorControlProcess.ExitCode }
  }
  foreach($identity in @($r.simulator_server_processes)) {
    $id=[int]$identity.id
    $observed=Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction SilentlyContinue
    if($observed -and $observed.CommandLine -ceq $identity.command_line) {
      $process=Get-Process -Id $id -ErrorAction SilentlyContinue
      if($process) { $r.simulator_control_forced=$true; Stop-Process -Id $id -Force }
    }
  }
  SaveReceipt
}
function ValidateReceipt {
  $vault=[IO.Path]::GetFullPath($r.vault)
  if((Split-Path $vault -Parent) -ine $transactionParent -or (Split-Path $vault -Leaf) -notlike 'combat-observer-*') { throw 'Unsafe or legacy transaction directory; existing backup vault is not writable through this runner.' }
  if($r.roots.Count -ne 2 -or $r.files.Count -ne 3) { throw 'Unexpected transaction shape.' }
  for($i=0;$i -lt 2;++$i) {
    $root=$r.roots[$i]
    if($root.original -ine $saveRoots[$i] -or $root.held -ine (Join-Path $vault "personal-root-$i") -or
       $root.testOutput -ine (Join-Path $vault "test-root-$i")) { throw 'Unsafe root target.' }
    foreach($path in @($root.original,$root.held,$root.testOutput)) {
      if(Test-Path -LiteralPath $path) {
        foreach($entry in @((Get-Item -LiteralPath $path -Force))+@(Get-ChildItem -LiteralPath $path -Force -Recurse)) {
          if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point refused during restoration.' }
        }
      }
    }
  }
  for($i=0;$i -lt 3;++$i) {
    if($r.files[$i].path -ine (Join-Path $game $installNames[$i]) -or
       $r.files[$i].backup -ine (Join-Path $vault $installNames[$i])) { throw 'Unsafe file target.' }
  }
}
function TrackSteamVrRuntime {
  if($InputMode -ne 'SyntheticHmd' -or -not $r.launch_requested) { return }
  $floor=([datetime]$r.launch_requested).ToUniversalTime()
  $total=0
  foreach($process in @(SteamVrProcesses)) {
    $started=$process.StartTime.ToUniversalTime()
    if($started -lt $floor) { throw 'Unexpected SteamVR runtime process predates this synthetic game launch.' }
    $existing=@($r.runtime_processes | Where-Object { [int]$_.id -eq $process.Id })
    if($existing.Count -and ($existing.Count -ne 1 -or $existing[0].path -ine $process.Path -or
       ([datetime]$existing[0].started_utc).Ticks -ne $started.Ticks)) { throw 'SteamVR runtime PID was reused or changed identity.' }
    if(-not $existing.Count) {
      $r.runtime_processes+=@([pscustomobject]@{id=$process.Id;path=$process.Path;started_utc=$started.ToString('o')})
      SaveReceipt
    }
    $mb=[math]::Ceiling($process.PrivateMemorySize64/1MB)
    if($r.runtime_peak_private_mb -isnot [Collections.IDictionary]) {
      $peak=@{}
      foreach($property in $r.runtime_peak_private_mb.PSObject.Properties) { $peak[$property.Name]=[int]$property.Value }
      SetReceiptValue 'runtime_peak_private_mb' $peak
    }
    $key="$($process.ProcessName):$($process.Id)"
    if(-not $r.runtime_peak_private_mb.Contains($key) -or $r.runtime_peak_private_mb[$key] -lt $mb) {
      $r.runtime_peak_private_mb[$key]=$mb; SaveReceipt
    }
    if($mb -gt 6144) { throw "SteamVR memory limit exceeded: $key uses $mb MiB (limit 6144)." }
    $total+=$mb
  }
  if($total -gt 12288) { throw "Combined SteamVR memory limit exceeded: $total MiB." }
}
function StopOwnedSteamVrRuntime {
  if($InputMode -ne 'SyntheticHmd') { return }
  TrackSteamVrRuntime
  foreach($identity in @($r.runtime_processes | Where-Object { [IO.Path]::GetFileName($_.path) -ieq 'vrmonitor.exe' })) {
    $process=Get-Process -Id ([int]$identity.id) -ErrorAction SilentlyContinue
    if($process -and $process.Path -ieq $identity.path -and $process.StartTime.ToUniversalTime().Ticks -eq ([datetime]$identity.started_utc).ToUniversalTime().Ticks) {
      $null=$process.CloseMainWindow()
    }
  }
  $deadline=[datetime]::UtcNow.AddSeconds(10); $forceAfter=[datetime]::UtcNow.AddSeconds(2)
  $r.runtime_forced_pids=@(); $quietSince=$null
  while([datetime]::UtcNow -lt $deadline) {
    TrackSteamVrRuntime
    $live=@()
    foreach($identity in @($r.runtime_processes)) {
      $process=Get-Process -Id ([int]$identity.id) -ErrorAction SilentlyContinue
      if($process -and $process.Path -ieq $identity.path -and
         $process.StartTime.ToUniversalTime().Ticks -eq ([datetime]$identity.started_utc).ToUniversalTime().Ticks) { $live+=@($process) }
    }
    if(-not $live.Count) {
      if(@(SteamVrProcesses).Count) { throw 'An unowned SteamVR runtime process remains; it was not stopped.' }
      if(-not $quietSince) { $quietSince=[datetime]::UtcNow }
      if(([datetime]::UtcNow-$quietSince).TotalSeconds -ge 1) { $r.runtime_cleanup_ok=$true; SaveReceipt; return }
      Start-Sleep -Milliseconds 100
      continue
    }
    $quietSince=$null
    if([datetime]::UtcNow -ge $forceAfter) {
      foreach($process in $live) {
        $r.runtime_forced_pids+=@($process.Id); SaveReceipt
        Stop-Process -Id $process.Id -Force
      }
    }
    Start-Sleep -Milliseconds 250
  }
  throw 'Owned SteamVR runtime processes did not stop within 10 seconds.'
}
function TrackGames {
  TrackSteamVrRuntime
  # Ownership floor: only generations created after this session's own launch
  # request can belong to us. Legacy receipts have no launch_requested, so
  # their staging start remains the floor. Never infer ownership solely from
  # the executable name plus a time after transaction start.
  $floor=if($r.launch_requested) { [datetime]$r.launch_requested } else { [datetime]$r.started }
  $live=@(Games)
  if($live.Count -gt 1) {
    throw 'Two game instances are live; this session launched one; do not change files beneath them.'
  }
  foreach($process in $live) {
    if($process.Path -ine (Join-Path $game 'DarkSoulsRemastered.exe') -or $process.StartTime -lt $floor) {
      throw 'Unexpected game process; do not change files beneath it.'
    }
    $existing=@($r.pids | Where-Object { $_.id -eq $process.Id })
    if($existing.Count -and ($existing.Count -ne 1 -or
       $existing[0].path -ine $process.Path -or
       ([datetime]$existing[0].start).Ticks -ne $process.StartTime.Ticks)) {
      throw 'Reused or contradictory game process identity; do not assume ownership.'
    }
    if(-not $existing.Count) {
      # Adoption is only possible while the session is still open. A fresh
      # generation after the finish request is excluded from stopping and
      # observation; RestoreTransaction then pauses and records that pause.
      if(FinishRequested) {
        throw 'Game generation appeared after the finish request; excluding the late launch from ownership.'
      }
      $r.pids+=@([pscustomobject]@{id=$process.Id;start=$process.StartTime.ToString('o');path=$process.Path})
      SaveReceipt
    }
  }
}
function WaitForSingleGameLaunch {
  # Steam can overlap its bootstrap and persistent game for a short interval.
  # Adopt neither while ambiguous. Steady-state TrackGames still refuses two
  # instances; startup must first settle to one exact generation for one second.
  $limit=(Get-Date).AddSeconds(90); $stable=$null; $stableSince=Get-Date
  while((Get-Date) -lt $limit -and (Get-Date) -lt [datetime]$r.deadline -and -not (FinishRequested)) {
    $live=@(Games)
    foreach($process in $live) {
      if($process.Path -ine (Join-Path $game 'DarkSoulsRemastered.exe') -or
          $process.StartTime -lt [datetime]$r.launch_requested) { throw 'Unexpected process during launch settling.' }
    }
    if($live.Count -eq 1) {
      $identity=([string]$live[0].Id)+':'+$live[0].StartTime.Ticks
      if($stable -eq $identity) {
        if(((Get-Date)-$stableSince).TotalMilliseconds -ge 1000) { TrackGames; return }
      } else { $stable=$identity; $stableSince=Get-Date }
    } else { $stable=$null }
    Start-Sleep -Milliseconds 250
  }
  throw 'Game launch did not settle to one stable generation within its bounded startup window.'
}
function WaitForGameSession {
  # Steam may replace a short-lived game bootstrap after an empty process poll.
  # Process disappearance is not session completion: keep the original fixed
  # protection window. Only the deadline or an explicit operator finish ends it.
  # TrackGames still refuses unexpected identities; never swallow that failure.
  while((Get-Date) -lt [datetime]$r.deadline -and -not (FinishRequested)) {
    TrackGames
    if($InputMode -eq 'OpenXRSimulator' -and $script:simulatorControlProcess) {
      $script:simulatorControlProcess.Refresh()
      if($script:simulatorControlProcess.HasExited) {
        if($script:simulatorControlProcess.ExitCode -ne 0) { throw "Simulator MCP controller exited with code $($script:simulatorControlProcess.ExitCode)." }
        return
      }
      SampleOpenXRSimulatorMemory
    }
    Start-Sleep -Milliseconds 500
  }
}
function ParseCases([string]$text) {
  if(-not $text) { return @() }
  $plan=@()
  foreach($entry in $text.Split(',')) {
    $pair=$entry.Trim().Split(':')
    if($pair.Count -ne 2) { throw "Case plan entry must be label:seconds: $entry" }
    $label=$pair[0].Trim(); $seconds=0
    if($label -notmatch '^[a-z][a-z0-9_-]{0,31}$') { throw "Case label is not a safe identifier: $label" }
    if(-not [int]::TryParse($pair[1].Trim(),[ref]$seconds) -or $seconds -lt 1 -or $seconds -gt 600) {
      throw "Case duration is out of range: $entry" }
    if($label -in @($plan | ForEach-Object { $_.label })) { throw "Case plan repeats label: $label" }
    $plan+=[pscustomobject]@{label=$label;seconds=$seconds}
  }
  return ,$plan
}
# Stopwatch.Timestamp is .NET Core only and Stopwatch.GetTimestamp() is absent from
# .NET Core, so an operator running 'powershell' (5.1) versus 'pwsh' would otherwise
# stamp null boundaries. Resolve whichever member this engine has, once.
$script:qpcMethod=[Diagnostics.Stopwatch].GetMethod('GetTimestamp')
function Qpc {
  if($script:qpcMethod) { return [long]$script:qpcMethod.Invoke($null,@()) }
  return [long][Diagnostics.Stopwatch]::Timestamp
}
# One stop marker, read by every wait loop. It used to be spelled inline three
# times, so a window could end for a reason the plan never recorded.
function FinishRequested {
  $path=$script:FinishPath
  return [bool]($path -and (Test-Path -LiteralPath $path))
}
# New trace files this transaction produced, with the one fact that decides
# whether each is usable evidence: a closing footer. An unclosed capture cannot
# be repaired later, so it is named while it is still visible.
# A capture's identity and closure live in its first and last records. Reading
# every line to find them made Restore linear in accumulated trace bytes (and
# the line-append itself quadratic), which cost tens of minutes per run as the
# game directory grew. Read only the head line and a bounded tail window.
function ReadFirstLine([string]$path) {
  $stream=$null;$reader=$null
  try {
    $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,
      ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader=[IO.StreamReader]::new($stream)
    return $reader.ReadLine()
  } finally {
    if($reader) { $reader.Dispose() } elseif($stream) { $stream.Dispose() }
  }
}
function ReadLastLine([string]$path) {
  $stream=$null
  try {
    $stream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,
      ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $length=$stream.Length
    if(-not $length) { return $null }
    $window=[int][Math]::Min($length,262144)
    $null=$stream.Seek(-$window,[IO.SeekOrigin]::End)
    $buffer=New-Object byte[] $window
    $read=0
    while($read -lt $window) {
      $n=$stream.Read($buffer,$read,$window-$read)
      if(-not $n) { break }
      $read+=$n
    }
    $text=[Text.Encoding]::UTF8.GetString($buffer,0,$read).TrimEnd("`r","`n"," ","`t")
    if(-not $text) { return $null }
    $nl=$text.LastIndexOf("`n")
    return $(if($nl -ge 0) { $text.Substring($nl+1) } else { $text })
  } finally { if($stream) { $stream.Dispose() } }
}
function TraceInventory {
  $spans=@()
  foreach($trace in @(Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-*.jsonl' |
                        Where-Object { $_.Name -notin $r.traces_before })) {
    $start=$null;$end=$null;$capturePid=$null;$footer=$null
    try {
      # FileShare ReadWrite+Delete: a capture this session is still writing is
      # open with write access, so ReadAllLines (share Read only) would fail
      # exactly while the footer question matters most.
      $firstLine=ReadFirstLine $trace.FullName
      if($firstLine) {
        $head=$firstLine|ConvertFrom-Json
        if($head.event -eq 'header') {
          $start=[long]$head.qpc
          if($head.pid) { $capturePid=[int]$head.pid }
        }
      }
      $lastLine=ReadLastLine $trace.FullName
      if($lastLine) {
        $last=$lastLine|ConvertFrom-Json
        if($last.event -eq 'footer') {
          $end=[long]$last.qpc
          $footer=[pscustomobject]@{interrupted=[bool]$last.interrupted;
            io_error=[bool]$last.io_error;dropped=[long]$last.dropped;events=[long]$last.events}
        }
      }
    } catch { }
    $spans+=,[pscustomobject]@{name=$trace.Name;start=$start;end=$end;pid=$capturePid;
      closed=$null -ne $end;footer=$footer}
  }
  # Plain array output: the comma form would hand back the array as one object and
  # every caller's .Count would read 1 however many captures exist.
  return $spans
}
function WaitForTraceStart {
  while((Get-Date) -lt [datetime]$r.deadline -and -not (FinishRequested)) {
    TrackGames
    if($InputMode -eq 'OpenXRSimulator' -and $script:simulatorControlProcess) {
      $script:simulatorControlProcess.Refresh()
      if($script:simulatorControlProcess.HasExited -and $script:simulatorControlProcess.ExitCode -ne 0) {
        throw "Simulator MCP controller exited with code $($script:simulatorControlProcess.ExitCode) before any trace started; see mcp stderr log in the run evidence."
      }
    }
    $liveIds=@(Games | ForEach-Object { [int]$_.Id })
    $ownedIds=@($r.pids | ForEach-Object { [int]$_.id })
    foreach($trace in @(Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-*.jsonl' -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -notin $r.traces_before })) {
      try {
        # @() wrapper: a one-line capture would otherwise unroll to a bare
        # string and $lines[0] would hand ConvertFrom-Json a single character.
        $firstLine=ReadFirstLine $trace.FullName
        if($firstLine) {
          $header=$firstLine | ConvertFrom-Json
          $capturePid=[int]$header.pid
          if($header.event -eq 'header' -and [long]$header.qpc -gt 0 -and
             $capturePid -in $ownedIds -and $capturePid -in $liveIds) {
            SetReceiptValue 'trace_started_file' $trace.Name
            SetReceiptValue 'trace_started_pid' $capturePid
            SetReceiptValue 'trace_started_qpc' ([long]$header.qpc)
            SaveReceipt
            Write-Output "TRACE START: $($trace.Name) header confirmed for owned PID $capturePid; case windows begin now."
            return
          }
        }
      } catch {
        # A failed read used to be swallowed forever; the first two failures are
        # recorded so a timeout names the real reason instead of only the deadline.
        if(-not $script:traceWaitErrors) { $script:traceWaitErrors=@() }
        if($script:traceWaitErrors.Count -lt 2) {
          $script:traceWaitErrors+="$($trace.Name): $($_.Exception.GetType().Name): $($_.Exception.Message)"
        }
      }
    }
    Start-Sleep -Milliseconds 250
  }
  $detail=if($script:traceWaitErrors) { " Last read failures: $($script:traceWaitErrors -join ' | ')" } else { '' }
  throw "Trace header for the owned game did not arrive before the fixed session deadline.$detail"
}
function TraceStopRequestPath([int]$processId) {
  return Join-Path $game ("DSVR-trace-stop-{0}.request" -f $processId)
}
function RemoveOwnedTraceStopRequests {
  foreach($processId in @($r.trace_stop_pids)) {
    $request=TraceStopRequestPath ([int]$processId)
    if(Test-Path -LiteralPath $request) {
      $expected="stop-v1:${processId}:$($r.trace_stop_token)`n"
      if([IO.File]::ReadAllText($request) -cne $expected) {
        throw "Trace-stop marker ownership mismatch for PID $processId; marker retained."
      }
      Remove-Item -LiteralPath $request -Force
    }
  }
}
function RequestTraceStop {
  $open=@(TraceInventory | Where-Object { -not $_.closed })
  if(-not $open.Count) { return }
  $live=@(Games)
  foreach($span in $open) {
    if($null -eq $span.pid) {
      Write-Warning "TRACE $($span.name): no valid PID in header; cannot request orderly close."
      continue
    }
    $tracked=@($r.pids | Where-Object { [int]$_.id -eq [int]$span.pid })
    $process=@($live | Where-Object { [int]$_.Id -eq [int]$span.pid })
    if($tracked.Count -ne 1 -or $process.Count -ne 1 -or
       $process[0].Path -ine $tracked[0].path -or
       $process[0].StartTime.Ticks -ne ([datetime]$tracked[0].start).Ticks) {
      Write-Warning "TRACE $($span.name): its PID is not a currently tracked process generation; no stop signal sent."
      continue
    }
    $request=TraceStopRequestPath $span.pid
    $alreadyRecorded=@($r.trace_stop_pids | Where-Object { [int]$_ -eq [int]$span.pid }).Count -gt 0
    if($alreadyRecorded -and (Test-Path -LiteralPath $request)) {
      $expected="stop-v1:$($span.pid):$($r.trace_stop_token)`n"
      if([IO.File]::ReadAllText($request) -cne $expected) {
        throw "Trace-stop marker ownership mismatch for PID $($span.pid); marker retained."
      }
      continue
    }
    if(-not $alreadyRecorded) {
      $r.trace_stop_pids+=@([int]$span.pid); SaveReceipt
    }
    $stream=$null
    try {
      $stream=[IO.File]::Open($request,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
      $bytes=[Text.Encoding]::ASCII.GetBytes("stop-v1:$($span.pid):$($r.trace_stop_token)`n")
      $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true)
    } catch {
      throw "Could not create unique trace-stop request for PID $($span.pid): $($_.Exception.Message)"
    } finally { if($stream) { $stream.Dispose() } }
    Write-Output "TRACE $($span.name): requested orderly writer close for PID $($span.pid)."
  }
}
function WaitForTraceFooters {
  $limit=(Get-Date).AddSeconds($FooterWaitSeconds)
  do {
    $spans=@(TraceInventory)
    $open=@($spans | Where-Object { -not $_.closed })
    if(-not $open.Count) { break }
    if(-not @(Games).Count) { break }
    Start-Sleep -Milliseconds 250
  } while((Get-Date) -lt $limit)
  foreach($span in @(TraceInventory)) {
    if(-not $span.closed) {
      Write-Warning "TRACE $($span.name) has no closing footer; it is incomplete evidence."
      continue
    }
    $quality=if($span.footer.io_error) { 'io-error' }
      elseif($span.footer.dropped -gt 0) { "dropped=$($span.footer.dropped)" }
      elseif($span.footer.interrupted) { 'interrupted' } else { 'complete' }
    Write-Output "TRACE $($span.name): footer observed ($quality, events=$($span.footer.events))."
  }
}
function WaitForNormalGameExit {
  if($InputMode -eq 'OpenXRSimulator') {
    Write-Output 'Trace footer is closed. Asking the exact owned DSR window to exit normally.'
    TrackGames
    foreach($identity in @($r.pids)) {
      $process=Get-Process -Id ([int]$identity.id) -ErrorAction SilentlyContinue
      if($process -and $process.Path -ieq $identity.path -and $process.StartTime.Ticks -eq ([datetime]$identity.start).Ticks) {
        $null=$process.CloseMainWindow()
      }
    }
  } else {
    Write-Output "Trace closure requested. Close Dark Souls Remastered normally within $CloseWaitMinutes minute(s); the runner will not terminate it."
  }
  $limit=(Get-Date).AddMinutes($CloseWaitMinutes)
  while((Get-Date) -lt $limit) {
    TrackGames
    if(-not @(Games).Count) { return }
    Start-Sleep -Milliseconds 500
  }
  if(@(Games).Count) {
    throw "Game remains live after $CloseWaitMinutes minute(s). Recovery is blocked; close it normally, then rerun -Mode Restore -Receipt '$Receipt'."
  }
}
function CloseTraceAndWaitForGame {
  StopOpenXRSimulatorControl
  RequestTraceStop
  WaitForTraceFooters
  ReportCaseCoverage
  WaitForNormalGameExit
}
function SaveCases {
  if(-not $CasesPath) { return }
  $temp=$CasesPath+'.tmp-'+$PID
  [IO.File]::WriteAllText($temp,(ConvertTo-Json $script:casePlan -Depth 8),[Text.UTF8Encoding]::new($false))
  $bak=$CasesPath+'.bak'
  if(Test-Path -LiteralPath $CasesPath) { [IO.File]::Replace($temp,$CasesPath,$bak); Remove-Item -LiteralPath $bak -Force } else { [IO.File]::Move($temp,$CasesPath) }
}
# Windows QPC is the same clock the capture header reports, so a boundary stamped
# here lands on the trace's own timeline. A label records only what the operator
# declared they were doing in that window; nothing here observes a native outcome,
# and an unfinished window keeps end_qpc null so the analyzer refuses to bind it.
function RunCasePlan($plan,$tick) {
  if(-not $plan -or -not @($plan).Count) { return }
  $script:casePlan=[ordered]@{schema=1;qpc_hz=[Diagnostics.Stopwatch]::Frequency;
    generated=(Get-Date).ToString('o');receipt=$Receipt;cases=@()}
  SaveCases
  foreach($entry in @($plan)) {
    $start=(Qpc)
    $row=[ordered]@{label=$entry.label;seconds=$entry.seconds;state='running';
      start_qpc=$start;end_qpc=$null}
    $script:casePlan.cases=@($script:casePlan.cases)+@($row)
    SaveCases
    $instruction=if($InputMode -eq 'SyntheticHmd') { 'Synthetic OpenXR inputs run automatically; no physical-hit claim is made. ' } else { 'Perform it now. ' }
    Write-Output ("CASE $($entry.label): $instruction" +
      "Capture window $($entry.seconds) seconds. The trace starts automatically after the local player appears. " +
      'A case outside a closed trace interval has no evidence.')
    $stop=(Get-Date).AddSeconds($entry.seconds)
    while((Get-Date) -lt $stop -and -not (FinishRequested)) {
      if($tick) { & $tick }
      Start-Sleep -Milliseconds 250
    }
    $row.end_qpc=(Qpc)
    # A window the stop request ended early is stamped on the same clock, with the
    # same fields, as one the operator had in full. The state is the only thing that
    # keeps 'the transaction ended first' from later reading as 'the attempt failed'.
    $row.state=if((Get-Date) -lt $stop) { 'truncated' } else { 'declared' }
    SaveCases
    if($row.state -eq 'truncated') {
      $elapsed=[math]::Round((($row.end_qpc-$start)/[Diagnostics.Stopwatch]::Frequency),1)
      Write-Warning "CASE $($entry.label): ended after $elapsed s of the $($entry.seconds) s declared. Partial opportunity, not a completed case."
    }
  }
}
# The analyzer reports coverage per capture file, which is after the game is closed
# and the transaction is over. Print it while the run is still open: a declared case
# that no capture overlaps can still be re-attempted, and after restoration that
# option is gone.
function ReportCaseCoverage {
  if(-not $script:casePlan -or -not @($script:casePlan.cases).Count) { return }
  $spans=@(TraceInventory)
  foreach($row in @($script:casePlan.cases)) {
    if($null -eq $row.end_qpc) { continue }
    $overlap=@($spans | Where-Object { $null -ne $_.end -and $_.start -le [long]$row.end_qpc -and
                                       $_.end -ge [long]$row.start_qpc })
    $full=@($overlap | Where-Object { $_.start -le [long]$row.start_qpc -and
                                      $_.end -ge [long]$row.end_qpc })
    Write-Output ("COVERAGE $($row.label): $(@($overlap).Count) capture(s) overlap, " +
      "$(@($full).Count) cover it completely, state=$($row.state).")
  }
  foreach($span in @($spans | Where-Object { -not $_.closed })) {
    Write-Warning "TRACE $($span.name) has no closing footer; it is incomplete evidence."
  }
}
function RestoreTransaction {
  ValidateReceipt
  # The observer writer must close normally before its trace is copied. Never
  # kill even a tracked generation; restoration beneath any live game is unsafe.
  if(@(Games).Count) { throw 'Game process is still live; close it normally before restoring. No files or save roots were changed.' }
  RemoveOwnedTraceStopRequests
  AssertFiles $r.files; AssertRoots $r.roots
  $runtimeCleanupError=$null
  if($InputMode -eq 'SyntheticHmd') {
    try { StopOwnedSteamVrRuntime; RestoreSteamVrSettings }
    catch { $runtimeCleanupError=$_.Exception.Message; SetReceiptValue 'runtime_cleanup_error' $runtimeCleanupError }
    try { SetReceiptValue 'runtime_registration_unchanged' ((Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime -eq $r.synthetic_layer.active_runtime) }
    catch { $runtimeCleanupError=$_.Exception.Message; SetReceiptValue 'runtime_cleanup_error' $runtimeCleanupError }
  } elseif($InputMode -eq 'OpenXRSimulator') {
    try {
      StopOpenXRSimulatorControl
      $after=(Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime
      $same=$after -ceq $r.simulator.system_runtime_before
      SetReceiptValue 'runtime_registration_unchanged' $same
      if(-not $same) { throw 'System OpenXR runtime registration changed during the process-overridden simulator run; the changed value was preserved.' }
      if($r.simulator_control_forced) { throw 'The simulator MCP controller or server required forced termination.' }
    } catch { $runtimeCleanupError=$_.Exception.Message; SetReceiptValue 'runtime_cleanup_error' $runtimeCleanupError }
  }
  foreach($trace in @(Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-*.jsonl')) {
    if($trace.Name -notin $r.traces_before) { Copy-Item -LiteralPath $trace.FullName -Destination $r.evidence }
  }
  # A capture the game never closed is incomplete evidence, and this is the last
  # moment it can be told apart from a finished one. Record which, never repair it.
  $unclosed=@(TraceInventory | Where-Object { -not $_.closed } | ForEach-Object { $_.name })
  if(@($unclosed).Count) {
    SetReceiptValue 'unclosed_traces' @($unclosed)
    foreach($name in @($unclosed)) { Write-Warning "TRACE $name has no closing footer; it is incomplete evidence." }
  }
  SaveReceipt
  if(Test-Path -LiteralPath (Join-Path $game 'DSVR.log')) {
    Copy-Item -LiteralPath (Join-Path $game 'DSVR.log') -Destination (Join-Path $r.evidence 'DSVR-native.log')
  }
  CopyTree $r.steam_data (Join-Path $r.vault 'steam-userdata-after') | Out-Null
  if(Test-Path -LiteralPath $r.steam_config) {
    Copy-Item -LiteralPath $r.steam_config -Destination (Join-Path $r.vault 'steam-localconfig-after.vdf')
  }
  # Steam owns its live cache. Preserve before/after snapshots, do NOT overwrite
  # newer in-memory/client metadata or stop/restart Steam to manufacture a match.
  RestoreRoots $r.roots
  RestoreFiles $r.files
  $r.state=if($runtimeCleanupError) { 'RecoveryBlocked' } else { 'Restored' }; SaveReceipt
  Write-Output "Restored and hash-verified original save roots and install files. Receipt: $Receipt"
  if($runtimeCleanupError) { throw "Save roots and game files restored, but simulator/runtime recovery is blocked: $runtimeCleanupError" }
}
function CompleteTransaction {
  # Do not let a tracking refusal skip safe cleanup, but first request a footer
  # and require normal process exit. RestoreTransaction independently refuses
  # any live game before touching roots/files.
  $trackingFailure=$null
  try { TrackGames } catch {
    $trackingFailure=$_
    SetReceiptValue 'tracking_error' $_.Exception.Message
  }
  try {
    if($r.roots.Count -eq 2 -and $r.files.Count -eq 3) {
      CloseTraceAndWaitForGame
      RestoreTransaction
    }
    else { $r.state='PreparationFailedBeforeStaging'; SaveReceipt; Write-Warning 'Preparation failed before staging; no roots/files were replaced.' }
  } catch {
    $r.state='RecoveryBlocked'
    SetReceiptValue 'recovery_error' $_.Exception.Message
    SaveReceipt
    throw
  }
  if($trackingFailure) { throw $trackingFailure }
}
function SetReceiptValue($name,$value) {
  # New transactions use a dictionary; Restore reads a PSCustomObject on both
  # PowerShell engines. Keep diagnostic fields writable for either shape.
  if($r -is [Collections.IDictionary]) { $r[$name]=$value }
  else { $r | Add-Member -NotePropertyName $name -NotePropertyValue $value -Force }
}
if($Mode -eq 'SelfTest') {
  $runtimeManifest=Join-Path $steam 'steamapps\common\SteamVR\steamxr_win64.json'
  if((RuntimeRootFromActiveRuntime $runtimeManifest) -ine (Join-Path $steam 'steamapps\common\SteamVR')) {
    throw 'Active-runtime path resolution returned the wrong SteamVR root.'
  }
  $badRuntimeRefused=$false
  try { RuntimeRootFromActiveRuntime (Join-Path $steam 'steamapps\common\SteamVR\other.json') | Out-Null }
  catch { $badRuntimeRefused=$true }
  if(-not $badRuntimeRefused) { throw 'Unknown active-runtime manifest was accepted.' }
  & {
    $fixture=Join-Path ([IO.Path]::GetTempPath()) ('dsvr-steamvr-settings-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($fixture)|Out-Null
    $path=Join-Path $fixture 'steamvr.vrsettings'
    $original=@{steamvr=@{forcedDriver='real';other=7}}
    $requested=[ordered]@{'steamvr.forcedDriver'='null';'steamvr.activateMultipleDrivers'=$false;'driver_null.enable'=$true}
    $updated=@{steamvr=@{forcedDriver='null';activateMultipleDrivers=$false;other=9};driver_null=@{enable=$true}}
    WriteJsonSettings $path $updated
    RestoreJsonSettings $path $original $requested
    $restored=Get-Content $path -Raw|ConvertFrom-Json -AsHashtable
    if($restored.steamvr.forcedDriver -ne 'real' -or $restored.steamvr.other -ne 9 -or
       $restored.steamvr.Contains('activateMultipleDrivers') -or $restored.Contains('driver_null')) {
      throw 'SteamVR setting restoration did not restore requested values or preserve unrelated updates.'
    }
    $updated.steamvr.forcedDriver='foreign'
    WriteJsonSettings $path $updated
    $refused=$false
    try { RestoreJsonSettings $path $original $requested } catch { $refused=$true }
    if(-not $refused -or (Get-Content $path -Raw|ConvertFrom-Json -AsHashtable).steamvr.forcedDriver -ne 'foreign') {
      throw 'SteamVR setting conflict was overwritten instead of refused.'
    }
    Remove-Item -LiteralPath $fixture -Recurse -Force
  }
  & {
    $base=[datetime]'2026-10-25T12:00:00Z'
    $path=Join-Path $game 'DarkSoulsRemastered.exe'
    $r=@{started=$base.ToString('o');pids=@([pscustomobject]@{id=10;start=$base.AddSeconds(1).ToString('o');path=$path})}
    $observed=[pscustomobject]@{Id=10;StartTime=$base.AddSeconds(1);Path=$path}
    function Games { @($observed) }
    function SaveReceipt { }
    TrackGames; TrackGames
    if($r.pids.Count -ne 1) { throw 'Repeated identity polling duplicated a receipt.' }
    $observed=[pscustomobject]@{Id=11;StartTime=$base.AddSeconds(2);Path=$path}
    TrackGames
    if($r.pids.Count -ne 2) { throw 'Distinct replacement generation was lost.' }
    $observed=[pscustomobject]@{Id=10;StartTime=$base.AddSeconds(2);Path=$path}
    $refused=$false
    try { TrackGames } catch { $refused=$true }
    if(-not $refused) { throw 'Reused PID silently inherited an old process receipt.' }
  }
  # Generation provenance. A matching exe name and start time after transaction
  # start is not ownership: only generations this session's own launch request
  # produced may be recorded and later stopped. A pre-request launch, a second
  # concurrent instance, or a generation appearing after the finish request all
  # refuse, while a legitimate bootstrap -> replacement chain still adopts.
  & {
    $base=[datetime]'2026-10-25T12:00:00Z'
    $path=Join-Path $game 'DarkSoulsRemastered.exe'
    $r=@{started=$base.ToString('o');launch_requested=$base.AddSeconds(60).ToString('o');
      pids=@([pscustomobject]@{id=10;start=$base.AddSeconds(61).ToString('o');path=$path})}
    function SaveReceipt { }
    $script:fixtureFinish=$false
    function FinishRequested { return [bool]$script:fixtureFinish }
    $observed=@()
    function Games { return @($observed) }
    # (a) Created before this session's launch request: someone else's game.
    $observed=@([pscustomobject]@{Id=20;StartTime=$base.AddSeconds(30);Path=$path})
    $refused=$false
    try { TrackGames } catch { $refused=$true }
    if(-not $refused -or $r.pids.Count -ne 1) { throw 'A process older than the launch request was adopted.' }
    # (b) Second concurrent instance while a tracked generation is live.
    $observed=@(
      [pscustomobject]@{Id=10;StartTime=$base.AddSeconds(61);Path=$path},
      [pscustomobject]@{Id=21;StartTime=$base.AddSeconds(90);Path=$path})
    $refused=$false
    try { TrackGames } catch { $refused=$true }
    if(-not $refused -or $r.pids.Count -ne 1) { throw 'A concurrent late Steam launch was adopted.' }
    # (c) Session is finishing; a fresh generation is not ours to stop or observe.
    $observed=@([pscustomobject]@{Id=22;StartTime=$base.AddSeconds(120);Path=$path})
    $script:fixtureFinish=$true
    $refused=$false
    try { TrackGames } catch { $refused=$true }
    if(-not $refused -or $r.pids.Count -ne 1) { throw 'A post-finish launch was adopted for stopping.' }
    $script:fixtureFinish=$false
    # (d) Sole replacement generation after the launch request: still adoptable.
    $observed=@([pscustomobject]@{Id=23;StartTime=$base.AddSeconds(70);Path=$path})
    TrackGames
    if($r.pids.Count -ne 2 -or 23 -notin @($r.pids.id)) { throw 'Legitimate replacement generation was refused.' }
  }
  & {
    $base=[datetime]'2026-10-25T12:00:00Z'
    $path=Join-Path $game 'DarkSoulsRemastered.exe'
    $r=[pscustomobject]@{state='ObserverReady';roots=@(1,2);files=@(1,2,3);
      pids=@([pscustomobject]@{id=10;start=$base.ToString('o');path=$path});trace_stop_pids=@()}
    $script:fixtureRestoreAttempted=$false; $script:fixtureReceiptSaved=$false
    $script:fixtureFilesTouched=$false; $script:fixtureKills=@(); $script:fixtureCloseInvoked=$false
    function TrackGames { throw 'fixture unexpected process' }
    function ValidateReceipt { $script:fixtureRestoreAttempted=$true }
    function CloseTraceAndWaitForGame { $script:fixtureCloseInvoked=$true }
    function Games { @([pscustomobject]@{Id=99}) }
    function AssertFiles { $script:fixtureFilesTouched=$true; throw 'Unsafe file access' }
    function SaveReceipt { $script:fixtureReceiptSaved=$true }
    try { CompleteTransaction } catch { }
    if(-not $script:fixtureRestoreAttempted -or -not $script:fixtureReceiptSaved -or -not $script:fixtureCloseInvoked -or
       $r.state -ne 'RecoveryBlocked' -or $script:fixtureFilesTouched -or
       $r.tracking_error -ne 'fixture unexpected process' -or
       $r.recovery_error -ne 'Game process is still live; close it normally before restoring. No files or save roots were changed.' -or
       $script:fixtureKills.Count) {
      throw ('Tracking refusal skipped safe recoverable cleanup: '+($r | ConvertTo-Json -Depth 5 -Compress)+
         ' kills='+($script:fixtureKills -join ',')+' attempted='+$script:fixtureRestoreAttempted+
         ' saved='+$script:fixtureReceiptSaved+' files='+$script:fixtureFilesTouched)
    }
  }
  # Exercise startup settling without weakening steady-state process refusal.
  & {
    $base=[datetime]'2026-10-26T12:00:00Z'; $script:settleElapsed=0
    $script:adopted=@(); $path=Join-Path $game 'DarkSoulsRemastered.exe'
    $r=@{deadline=$base.AddSeconds(150);launch_requested=$base.ToString('o')}
    function Get-Date { $base.AddMilliseconds($script:settleElapsed) }
    function Start-Sleep { param($Milliseconds) $script:settleElapsed+=$Milliseconds }
    function FinishRequested { $false }
    function Games {
      if($script:settleElapsed -lt 500) {
        return @([pscustomobject]@{Id=1;Path=$path;StartTime=$base.AddTicks(1)},
                 [pscustomobject]@{Id=2;Path=$path;StartTime=$base.AddTicks(2)})
      }
      if($script:settleElapsed -lt 750) { return @() }
      $id=if($script:settleElapsed -lt 1000) { 3 } else { 4 }
      return @([pscustomobject]@{Id=$id;Path=$path;StartTime=$base.AddTicks($id)})
    }
    function TrackGames { $script:adopted+=@((Games)[0].Id) }
    WaitForSingleGameLaunch
    if($script:settleElapsed -ne 2000 -or $script:adopted.Count -ne 1 -or $script:adopted[0] -ne 4) {
      throw 'Transient, empty or changing startup generation was adopted.'
    }
    $script:settleElapsed=0; $script:adopted=@()
    function Games { @([pscustomobject]@{Id=9;Path=$path;StartTime=$base.AddTicks(-1)}) }
    $refused=$false
    try { WaitForSingleGameLaunch } catch { $refused=$_.Exception.Message -eq 'Unexpected process during launch settling.' }
    if(-not $refused -or $script:adopted.Count) { throw 'Pre-existing game was adopted while settling.' }
  }
  # Exercise the production monitor with a deterministic bootstrap -> empty ->
  # replacement timeline. No Steam, real processes, installs or saves are touched.
  & {
    $base=[datetime]'2026-10-25T12:00:00Z'; $script:fixtureElapsed=0
    $script:fixtureVisits=@()
    $r=@{deadline=$base.AddSeconds(5);pids=@(@{id=1})}
    function Get-Date { $base.AddMilliseconds($script:fixtureElapsed) }
    function Start-Sleep { param($Milliseconds)
      $script:fixtureElapsed+=$Milliseconds
      if($script:fixtureElapsed -gt 10000) { throw 'Fixture monitor did not stop.' }
    }
    function FinishRequested { $false }
    function TrackGames {
      $script:fixtureVisits+=,$script:fixtureElapsed
      if($script:fixtureElapsed -eq 1000) { $r.pids+=@(@{id=2}) }
    }
    function Games {
      if($script:fixtureElapsed -eq 500) { return @() }
      return @([pscustomobject]@{Id=2})
    }
    WaitForGameSession
    if($script:fixtureElapsed -ne 5000 -or 2 -notin @($r.pids.id)) {
      throw "Bootstrap exit ended protection before the replacement: elapsed=$script:fixtureElapsed ms."
    }
    # Explicit finish stops promptly, without extending the recorded deadline.
    $script:fixtureElapsed=0; $script:fixtureVisits=@()
    function FinishRequested { $script:fixtureElapsed -ge 1500 }
    WaitForGameSession
    if($script:fixtureElapsed -ne 1500 -or $r.deadline -ne $base.AddSeconds(5)) {
      throw 'Explicit finish or immutable session deadline was lost.'
    }
    # A refused process remains an error, never a successful monitor completion.
    $script:fixtureElapsed=0
    function FinishRequested { $false }
    function TrackGames { throw 'fixture unexpected process' }
    $refused=$false
    try { WaitForGameSession } catch {
      if($_.Exception.Message -ne 'fixture unexpected process') { throw }
      $refused=$true
    }
    if(-not $refused) { throw 'Process refusal was swallowed.' }
  }
  $dir=Join-Path $env:LOCALAPPDATA ('Temp\opencode\combat-transaction-test-'+[guid]::NewGuid())
  [IO.Directory]::CreateDirectory($dir) | Out-Null
  $childScript=Join-Path $dir 'detached-child.ps1'
  $childStdout=Join-Path $dir 'detached.stdout.log'; $childStderr=Join-Path $dir 'detached.stderr.log'
  $childSource=@'
param([string]$Message)
Start-Sleep -Milliseconds 250
Write-Output $Message
[Console]::Error.WriteLine('stderr-fixture')
'@
  [IO.File]::WriteAllText($childScript,$childSource,[Text.UTF8Encoding]::new($false))
  $message='argument with spaces and "quotes"'
  $childArgsFixture=@('-NoProfile','-NoLogo','-NonInteractive','-File',$childScript,'-Message',$message)
  $detached=StartDetachedPowerShell $childArgsFixture $childStdout $childStderr $dir
  if($detached.Id -le 0) { throw 'Detached launcher did not return a child process ID.' }
  Wait-Process -Id $detached.Id -Timeout 10
  if((Get-Content -LiteralPath $childStdout -Raw).Trim() -cne $message -or
     (Get-Content -LiteralPath $childStderr -Raw) -notmatch 'stderr-fixture') {
    throw 'Detached child argument quoting or redirected logs failed.'
  }
  $Receipt=Join-Path $dir 'receipt.json'; $script:r=[ordered]@{state='Fixture'}
  SaveReceipt; $initialReceiptHash=Hash $Receipt
  $r.state='FixtureUpdated'; SaveReceipt
  if((Hash $Receipt) -ne $initialReceiptHash) { throw 'Immutable receipt was overwritten.' }
  $original=Join-Path $dir 'installed'; $backup=Join-Path $dir 'backup'; $candidate=Join-Path $dir 'candidate'
  [IO.File]::WriteAllText($original,'personal'); Copy-Item $original $backup
  [IO.File]::WriteAllText($candidate,'observer')
  $file=[pscustomobject]@{path=$original;backup=$backup;originalHash=(Hash $original);expectedHash=(Hash $original)}
  StageFile $file $candidate; RestoreFiles @($file); RestoreFiles @($file)
  if([IO.File]::ReadAllText($original) -ne 'personal') { throw 'File restoration failed.' }
  StageFile $file $candidate; [IO.File]::WriteAllText($original,'foreign newer work')
  $refused=$false
  try { RestoreFiles @($file) } catch { $refused=$true }
  if(-not $refused -or [IO.File]::ReadAllText($original) -ne 'foreign newer work') { throw 'Conflict was overwritten.' }
  $root=Join-Path $dir 'save'; $seed=Join-Path $dir 'seed'
  [IO.Directory]::CreateDirectory($root) | Out-Null; [IO.Directory]::CreateDirectory($seed) | Out-Null
  [IO.File]::WriteAllText((Join-Path $root 'personal.sl2'),'personal save')
  [IO.File]::WriteAllText((Join-Path $seed 'disposable.sl2'),'disposable seed')
  $entry=[pscustomobject]@{original=$root;held=(Join-Path $dir 'held');testOutput=(Join-Path $dir 'test-output');seed=$seed;inventory=@(Inventory $root);moved=$false}
  StageRoots @($entry)
  if(Test-Path (Join-Path $root 'personal.sl2')) { throw 'Personal save leaked into test root.' }
  [IO.File]::WriteAllText((Join-Path $root 'disposable.sl2'),'game progress')
  RestoreRoots @($entry); RestoreRoots @($entry)
  if([IO.File]::ReadAllText((Join-Path $root 'personal.sl2')) -ne 'personal save' -or
     [IO.File]::ReadAllText((Join-Path $entry.testOutput 'disposable.sl2')) -ne 'game progress') { throw 'Save restoration/lifetime failed.' }
  $refused=$false
  try { StageRoots @($entry) } catch { $refused=$true }
  if(-not $refused -or -not (Test-Path (Join-Path $root 'personal.sl2'))) { throw 'Existing staging target was overwritten.' }
  if(([regex]::Matches((ObserverConfig),'(?m)^observer_only\s*=')).Count -ne 1) { throw 'Duplicate observer setting.' }
  $refused=$false
  foreach($bad in @('hit','hit:','hit:x','hit:0','hit:601','hit:5,hit:6',':5','hit 5:5','hit:5,')) {
    try { ParseCases $bad | Out-Null } catch { $refused=$true; continue }
    throw "Malformed case plan accepted: $bad"
  }
  if(-not $refused) { throw 'No malformed case plan was refused.' }
  if((ParseCases '').Count) { throw 'Empty case plan produced windows.' }
  & {
    $fixtureGame=Join-Path $dir 'trace-start-order'
    [IO.Directory]::CreateDirectory($fixtureGame) | Out-Null
    $base=[datetime]'2026-10-30T12:00:00Z'; $script:caseFixtureElapsed=0; $script:caseFixtureQpc=1000
    $oldGame=$script:game; $oldCasesPath=$script:CasesPath; $oldReceipt=$script:r
    $script:game=$fixtureGame; $script:CasesPath=Join-Path $fixtureGame 'cases.json'
    $script:r=[ordered]@{deadline=$base.AddSeconds(5);traces_before=@();pids=@([pscustomobject]@{id=42})}
    function Get-Date { $base.AddMilliseconds($script:caseFixtureElapsed) }
    function Start-Sleep { param($Milliseconds)
        $script:caseFixtureElapsed+=$Milliseconds
        $trace=Join-Path $fixtureGame 'DSVR-trace-delayed.jsonl'
        if($script:caseFixtureElapsed -ge 500 -and -not (Test-Path -LiteralPath $trace)) {
          $header=[ordered]@{event='header';qpc=[long]$script:caseFixtureQpc;pid=42}
          [IO.File]::WriteAllText($trace,(ConvertTo-Json $header -Compress))
        }
    }
    function Qpc { $script:caseFixtureQpc+=100; return [long]$script:caseFixtureQpc }
      function FinishRequested { $false }
    function TrackGames { }
    function Games { @([pscustomobject]@{Id=42}) }
      function SaveReceipt { }
    try {
      WaitForTraceStart | Out-Null
      RunCasePlan (ParseCases 'natural_hit:1') {}
      $case=(Get-Content -LiteralPath $script:CasesPath -Raw | ConvertFrom-Json).cases[0]
      $span=@(TraceInventory | Where-Object { $_.pid -eq 42 })[0]
      if($script:caseFixtureElapsed -lt 500 -or -not $span -or $case.start_qpc -lt $span.start) {
        throw 'Case window started before the owned trace header.'
      }
    } finally {
      $script:game=$oldGame; $script:CasesPath=$oldCasesPath; $script:r=$oldReceipt
    }
  }
  $CasesPath=Join-Path $dir 'combat-cases.json'
  RunCasePlan (ParseCases 'hit:1,miss-block:1') {}
  $written=Get-Content -LiteralPath $CasesPath -Raw | ConvertFrom-Json
  $rows=@($written.cases)
  if($written.schema -ne 1 -or $written.qpc_hz -le 0 -or $rows.Count -ne 2) { throw 'Case plan clock or windows were not recorded.' }
  if($rows[0].label -ne 'hit' -or $rows[1].label -ne 'miss-block' -or $rows[0].state -ne 'declared') { throw 'Case labels or completion state lost.' }
  foreach($row in $rows) {
    if(($row.start_qpc -isnot [long] -and $row.start_qpc -isnot [int]) -or
       ($row.end_qpc -isnot [long] -and $row.end_qpc -isnot [int]) -or $row.start_qpc -le 0) {
      throw 'Case boundary is not a real QPC reading.' }
    if($row.end_qpc -lt $row.start_qpc) { throw 'Case window is not monotonic on the QPC clock.' } }
  if($rows[0].end_qpc -le $rows[0].start_qpc) { throw 'Case window has no measured duration.' }
  if($rows[1].start_qpc -lt $rows[0].end_qpc -or $rows[1].end_qpc -le $rows[0].end_qpc) {
    throw 'Case windows overlap or share one QPC clock incorrectly.' }
  # Cross-tool round trip: the analyzer must bind the windows this runner wrote,
  # on the clock this runner reported. One complete triplet is placed inside each
  # window and one outside both, so a wrong clock, a label-agnostic bucket or a
  # window that swallows its neighbour shows up as a wrong per-case count.
  $python=Get-Command python -ErrorAction SilentlyContinue
  if(-not $python) { throw 'Case binding round trip needs python on PATH (ctest registers it).' }
  $hz=[long]$written.qpc_hz
  function Triplet($qpc,$seq,$hp) {
    $shared=[ordered]@{qpc=$qpc;seq=$seq;thread=7;object='1403c4020';flags=15;native_rate=0x12345678}
    $contact=[ordered]@{event='native_combat_contact';duration_qpc=50;
      values=@(1,0,0,0,2,0,0,0,1000,1000,120,120,400,(400+$hp),100,100)}
    $identity=[ordered]@{event='native_combat_identity';
      values=@(1,0,0,0,2,0,0,0,0x5678,0x1234,0xabcd,0,0x5678,0x1234,0,0)}
    $diag=[ordered]@{event='native_combat_diagnostics';duration_qpc=10;flags=0;values=@(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)}
    foreach($part in @($contact,$identity,$diag)) { $copy=[ordered]@{}
      foreach($key in $shared.Keys) { $copy[$key]=$shared[$key] }
      foreach($key in $part.Keys) { $copy[$key]=$part[$key] }
      $copy }
  }
  $body=@([ordered]@{event='tracking';qpc=[long]$rows[0].start_qpc;seq=7;epoch=1;predicted_ns=900})
  # -shr, not /2: PowerShell promotes an odd QPC sum to a double, and a
  # fractional qpc is a malformed record to the analyzer. The parity of a real
  # QPC reading varies per run, so /2 would make this test fail at random.
  $body+=Triplet ((([long]$rows[0].start_qpc+[long]$rows[0].end_qpc) -shr 1)) 1 (-25)
  $body+=Triplet ((([long]$rows[1].start_qpc+[long]$rows[1].end_qpc) -shr 1)) 2 0
  $body+=Triplet (([long]$rows[1].end_qpc)+1000) 3 (-7)
  $records=@([ordered]@{event='header';qpc=([long]$rows[0].start_qpc)-1000;schema=2;qpc_hz=$hz;
    capture_domain='combat';combat_hook=1;combat_detail_schema=1})
  $records+=$body
  $records+=[ordered]@{event='footer';qpc=([long]$rows[1].end_qpc)+2000;dropped=0;
    events=$body.Count;interrupted=$false;io_error=$false}
  $capturePath=Join-Path $dir 'DSVR-trace-selftest.jsonl'
  [IO.File]::WriteAllText($capturePath,(($records|ForEach-Object { ConvertTo-Json $_ -Compress }) -join "`n"),[Text.UTF8Encoding]::new($false))
  $previous=$ErrorActionPreference; $ErrorActionPreference='Continue'
  $report=(& $python.Source (Join-Path $repo 'tools\vr_trace.py') $capturePath 2>&1 | Out-String)
  $code=$LASTEXITCODE; $ErrorActionPreference=$previous
  if($code -ne 2) { throw "Analyzer exited $code on the round-trip capture, not 2 (incomplete)." }
  $parsed=$report|ConvertFrom-Json
  if(-not $parsed.valid -or $parsed.errors.Count) { throw "Round-trip capture rejected: $($parsed.errors -join '; ')" }
  if($parsed.checks.combat_observation_integrity.status -ne 'PASS') { throw 'Round-trip combat integrity not PASS.' }
  if($parsed.combat_case_plan.windows -ne 2 -or $parsed.native_combat_observations.paired_samples -ne 3) {
    throw 'Round trip lost a case window or a paired sample.' }
  $obs=$parsed.native_combat_observations.case_observations
  if($obs.hit.samples -ne 1 -or $obs.'miss-block'.samples -ne 1) { throw 'Case windows did not bind one sample each.' }
  if($obs.hit.samples_with_hp_loss -ne 1 -or $obs.'miss-block'.samples_with_hp_loss -ne 0) {
    throw 'Per-case HP evidence does not match the declared windows.' }
    if($obs.hit.diagnostic_samples -ne 1 -or $obs.hit.read_reason_counts.valid -ne 4) {
    throw 'Per-case actor-read reasons not attributed to the declared window.' }
   if($parsed.combat_case_plan.coverage_status -ne 'PASS' -or @($parsed.combat_case_plan.unobserved_labels).Count) {
    throw 'Round-trip capture did not cover its declared case windows.' }
    # The operator-facing coverage line is the only output that can still change the
    # run, so it is exercised against the same capture the receipt would copy. It
    # reads the trace files from the game directory, so point that at the fixture.
    $game=$dir
    $coverageText=@(ReportCaseCoverage) -join ' / '
    if($coverageText -notmatch 'COVERAGE hit: 1 capture\(s\) overlap, 1 cover it completely, state=declared' -or
      $coverageText -notmatch 'COVERAGE miss-block: 1 capture\(s\) overlap, 1 cover it completely, state=declared') {
    throw "Live coverage report did not match the fixture capture: $coverageText" }
    # The receipt's unclosed_traces field is only as good as this footer test, so
    # test the test: a capture with a footer is closed, a header-only capture is not.
    [IO.File]::WriteAllText((Join-Path $dir 'DSVR-trace-unclosed.jsonl'),
     (ConvertTo-Json $records[0] -Compress))
    $inventory=@(TraceInventory)
    $open=@($inventory | Where-Object { -not $_.closed } | ForEach-Object { $_.name })
    if($inventory.Count -ne 2 -or @($inventory | Where-Object { $_.closed }).Count -ne 1 -or
      $open.Count -ne 1 -or $open[0] -ne 'DSVR-trace-unclosed.jsonl') {
    throw ('Unclosed-capture detection wrong: total='+$inventory.Count+' open='+($open -join ',')) }
    # Its header still gives a start time, so an unclosed capture could silently count
    # as coverage. It must not: the overlap count stays at the one closed capture.
    $afterText=@(ReportCaseCoverage) -join ' / '
    if($afterText -notmatch 'COVERAGE hit: 1 capture\(s\) overlap, 1 cover it completely') {
    throw "Unclosed capture counted as coverage: $afterText" }
    & {
      $game=$dir; $fixturePid=98765; $fixtureStart=[datetime]'2026-10-26T12:00:00Z'
      $fixturePath=Join-Path $game 'DarkSoulsRemastered.exe'
      $r=[ordered]@{traces_before=@('DSVR-trace-selftest.jsonl','DSVR-trace-unclosed.jsonl');pids=@([pscustomobject]@{id=$fixturePid;
        start=$fixtureStart.ToString('o');path=$fixturePath});trace_stop_pids=@();trace_stop_token='fixture-token'}
      $fixtureTrace=Join-Path $game 'DSVR-trace-stop-fixture.jsonl'
      $header=[ordered]@{event='header';qpc=100;pid=$fixturePid}
      [IO.File]::WriteAllText($fixtureTrace,(ConvertTo-Json $header -Compress),[Text.UTF8Encoding]::new($false))
      function Games { @([pscustomobject]@{Id=$fixturePid;Path=$fixturePath;StartTime=$fixtureStart}) }
      function SaveReceipt { }
      RequestTraceStop
      $request=TraceStopRequestPath $fixturePid
      if(-not (Test-Path -LiteralPath $request) -or $r.trace_stop_pids.Count -ne 1 -or
         [IO.File]::ReadAllText($request) -ne "stop-v1:${fixturePid}:fixture-token`n") {
        throw 'Process-scoped trace-stop request was not created and receipted.'
      }
      RequestTraceStop
      if($r.trace_stop_pids.Count -ne 1) { throw 'Repeated close polling duplicated a stop request.' }
      Remove-Item -LiteralPath $request -Force # Simulate a crash before marker consumption.
      RequestTraceStop
      if(-not (Test-Path -LiteralPath $request) -or $r.trace_stop_pids.Count -ne 1) {
        throw 'Retry did not recreate a missing recorded stop marker.'
      }
      RemoveOwnedTraceStopRequests # Simulate cleanup after the diagnostics thread consumes it.
      if(Test-Path -LiteralPath $request) { throw 'Owned stop marker was not cleaned after process exit.' }
      $foreignRequest=TraceStopRequestPath 98766
      [IO.File]::WriteAllText($foreignRequest,'stop-v1:98766:foreign-token`n',[Text.Encoding]::ASCII)
      $r.trace_stop_pids=@(98766)
      $refused=$false
      try { RemoveOwnedTraceStopRequests } catch { $refused=$true }
      if(-not $refused -or -not (Test-Path -LiteralPath $foreignRequest)) {
        throw 'Foreign stop marker was deleted instead of refused.'
      }
      Remove-Item -LiteralPath $foreignRequest -Force
      $r.trace_stop_pids=@($fixturePid)
      $footer=[ordered]@{event='footer';qpc=200;interrupted=$false;io_error=$false;dropped=0;events=1}
      [IO.File]::AppendAllText($fixtureTrace,"`n"+(ConvertTo-Json $footer -Compress),[Text.UTF8Encoding]::new($false))
      WaitForTraceFooters
      $span=@(TraceInventory | Where-Object { $_.name -eq 'DSVR-trace-stop-fixture.jsonl' })[0]
      if(-not $span.closed -or $span.pid -ne $fixturePid -or $span.footer.events -ne 1 -or
         $span.footer.io_error -or $span.footer.dropped) {
        throw 'Footer polling did not preserve parsed PID/quality fields.'
      }
    }
    # A stop request landing mid-window must survive as a truncated window: the same
   # QPC boundaries otherwise read as a case the operator had in full.
   $script:FinishPath=Join-Path $dir 'finish.request'
   [IO.File]::WriteAllText($script:FinishPath,'stop')
   $CasesPath=Join-Path $dir 'combat-cases-truncated.json'
   RunCasePlan (ParseCases 'long:300') {}
   $cut=@((Get-Content -LiteralPath $CasesPath -Raw | ConvertFrom-Json).cases)
   $script:FinishPath=$null
   if($cut.Count -ne 1 -or $cut[0].state -ne 'truncated') { throw 'A stop-requested window was not marked truncated.' }
   if((([long]$cut[0].end_qpc-[long]$cut[0].start_qpc)/[Diagnostics.Stopwatch]::Frequency) -ge 30) {
    throw 'A truncated window still ran its declared duration.' }
   Remove-Item -LiteralPath (Join-Path $dir 'finish.request') -Force
  Write-Output "PASS: actual file/root staging, idempotent rollback, conflict refusal, unique observer config, QPC-stamped case plan, analyzer round trip. Fixtures: $dir"
  exit 0
}
if(-not $Receipt) { throw 'Provide a unique -Receipt under build/verification.' }
$Receipt=[IO.Path]::GetFullPath($Receipt)
$evidence=Split-Path $Receipt -Parent
$allowed=[IO.Path]::GetFullPath((Join-Path $repo 'build\verification'))+[IO.Path]::DirectorySeparatorChar
if(-not $Receipt.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Receipt must be under build/verification.' }
$script:FinishPath=$Receipt+'.finish'
if($Mode -eq 'Launch') {
  $launchPath=$Receipt+'.launch.json'; $stdoutPath=$Receipt+'.stdout.log'; $stderrPath=$Receipt+'.stderr.log'
  foreach($path in @($Receipt,$launchPath,$stdoutPath,$stderrPath,$script:FinishPath)) {
    if(Test-Path -LiteralPath $path) { throw "Launch artifact already exists: $path" }
  }
  [IO.Directory]::CreateDirectory($evidence) | Out-Null
  $childArgs=@('-NoProfile','-NoLogo','-NonInteractive','-File',$PSCommandPath,
    '-Mode','Run','-Receipt',$Receipt,'-CandidateReceipt',$CandidateReceipt,
    '-WindowMinutes',"$WindowMinutes",'-CloseWaitMinutes',"$CloseWaitMinutes",
    '-FooterWaitSeconds',"$FooterWaitSeconds",'-SeedVault',$SeedVault,
    '-InputMode',$InputMode,'-HmdLayerDir',$HmdLayerDir,
    '-SimulatorSoakSeconds',"$SimulatorSoakSeconds",'-MenuBudgetSeconds',"$MenuBudgetSeconds")
  if($Cases) { $childArgs+=@('-Cases',$Cases) }
  $child=StartDetachedPowerShell $childArgs $stdoutPath $stderrPath $repo
  $launch=[ordered]@{schema=1;receipt=$Receipt;pid=$child.Id;started=(Get-Date).ToString('o');
    stdout=$stdoutPath;stderr=$stderrPath;mode='Run';input_mode=$InputMode}
  try { WriteNewReceiptJson $launchPath $launch }
  catch { throw "Detached child PID $($child.Id) started but launch record could not be written. Logs: $stdoutPath ; $stderrPath. $($_.Exception.Message)" }
  Write-Output "Detached protected runner PID $($child.Id). Logs: $stdoutPath ; $stderrPath. Finish with: pwsh -NoProfile -File `"$PSCommandPath`" -Mode Finish -Receipt `"$Receipt`". Close the game normally after the footer is reported."
  exit 0
}
if($Mode -eq 'Finish') {
  if(-not (Test-Path -LiteralPath $Receipt)) { throw 'Run receipt not found.' }
  $script:r=ReadReceipt
  if($r.state -in @('Restored','RecoveryBlocked')) { throw "Run is not finishable from receipt state '$($r.state)'. Use -Mode Restore after the game exits if recovery is blocked." }
  $lockPath=Join-Path $transactionParent 'active-session.lock'
  $lease=$null; $active=$false
  try { $lease=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
  catch {
    # [IO.File]::Open wraps the IOException in a MethodInvocationException;
    # the sharing-violation HResult lives on the inner exception.
      $inner=$_.Exception
      while($inner -and $inner -isnot [System.IO.IOException]) { $inner=$inner.InnerException }
      $nativeError=if($inner) { $inner.HResult -band 0xffff } else { $_.Exception.HResult -band 0xffff }
    if($nativeError -in @(32,33)) { $active=$true } else { throw }
  }
  if($lease) { $lease.Dispose() }
  if(-not $active) { throw 'No active capture runner owns the transaction lease.' }
  if(Test-Path -LiteralPath $script:FinishPath) {
    Write-Output "Finish was already requested for $Receipt."
    exit 0
  }
  $stream=$null
  try {
    $stream=[IO.File]::Open($script:FinishPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $bytes=[Text.Encoding]::ASCII.GetBytes("finish-v1`n")
    $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true)
  } finally { if($stream) { $stream.Dispose() } }
  Write-Output "Finish requested. The runner will close the trace writer; close the game normally when prompted."
  exit 0
}
if($Mode -eq 'Restore') {
  [IO.Directory]::CreateDirectory($transactionParent) | Out-Null
  $script:transactionLease=[IO.File]::Open((Join-Path $transactionParent 'active-session.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  $script:r=ReadReceipt
  if($r.input_mode -eq 'SyntheticHmd') {
    $InputMode='SyntheticHmd'
    $script:runtimeRoot=RuntimeRootFromActiveRuntime $r.synthetic_layer.active_runtime
    $script:steamVrSettingsPath=$r.runtime_settings_path
    $requested=[ordered]@{}
    foreach($property in $r.runtime_settings_requested.PSObject.Properties) { $requested[$property.Name]=$property.Value }
    $script:steamVrRequested=$requested
    $script:hmdMutex=[Threading.Mutex]::new($false,'Local\DSVR-HmdLab')
    $acquired=$false
    try { $acquired=$script:hmdMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired=$true }
    if(-not $acquired) { $script:hmdMutex.Dispose(); $script:hmdMutex=$null; throw 'The standalone HMD lab is active; refusing runtime recovery concurrently.' }
  } elseif($r.input_mode -eq 'OpenXRSimulator') {
    $InputMode='OpenXRSimulator'
    $script:simulatorDataDir=$r.simulator.data_dir
    $script:simulatorMemoryPath=Join-Path $r.evidence 'memory.csv'
    $script:simulatorRuntimeLock=OpenXRSimulatorLock
    if($r.simulator_control_id) {
      $control=Get-Process -Id ([int]$r.simulator_control_id) -ErrorAction SilentlyContinue
      if($control -and $control.Path -ieq $r.simulator_control_path -and
         $control.StartTime.ToUniversalTime().Ticks -eq ([datetime]$r.simulator_control_started_utc).ToUniversalTime().Ticks) {
        $script:simulatorControlProcess=$control
      }
    }
    $script:hmdMutex=[Threading.Mutex]::new($false,'Local\DSVR-HmdLab')
    $acquired=$false
    try { $acquired=$script:hmdMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired=$true }
    if(-not $acquired) { $script:hmdMutex.Dispose(); $script:hmdMutex=$null; throw 'Another OpenXR-Simulator run is active; refusing transaction recovery concurrently.' }
  }
  try { RestoreTransaction }
  finally { if($script:hmdMutex) { $script:hmdMutex.ReleaseMutex(); $script:hmdMutex.Dispose(); $script:hmdMutex=$null } }
  exit 0
}
# Entire preflight is read-only. Never infer safety from a prior run's receipt.
if(@(Games).Count) { throw 'Game must be closed.' }
if(Test-Path -LiteralPath $script:FinishPath) { throw 'Finish marker already exists for this receipt.' }
$staleStopMarkers=@(Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-stop-*.request' -ErrorAction SilentlyContinue)
if($staleStopMarkers.Count) { throw "Stale trace-stop request exists; reconcile its owning receipt before a new run: $($staleStopMarkers[0].Name)" }
if((Get-ItemProperty 'HKCU:\Software\Valve\Steam\ActiveProcess').ActiveUser -ne $account) { throw 'Active Steam account changed.' }
if(-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Steam must already be running in Offline mode.' }
$login=Get-Content -LiteralPath (Join-Path $steam 'config\loginusers.vdf') -Raw
$user=[regex]::Match($login,'"76561199803506767"\s*\{([^{}]*)\}')
if(-not $user.Success -or $user.Groups[1].Value -notmatch '"WantsOfflineMode"\s*"1"') { throw 'Steam Offline preference is not set.' }
$connection=Get-Content -LiteralPath (Join-Path $steam 'logs\connection_log.txt') -Tail 120
$state=@($connection | Select-String '\[(Logged On|Logged Off|Logging On|Logging Off|Connecting|Connected),') | Select-Object -Last 1
if(-not $state -or $state.Line -notmatch '\[Logged Off,') { throw 'Current Steam client is not observed logged off. Use Steam Offline first.' }
# Offline safety is Steam Offline (checked above) plus the game
# own Offline Mode, confirmed by the operator on the title screen
# before play. Offline Mode makes no online connections until the
# player turns it back on in the game; no firewall rules are
# created or required.
if((Hash (Join-Path $game 'DarkSoulsRemastered.exe')) -ne 'a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b') { throw 'Unknown executable.' }
$candidateReceiptPath=if([IO.Path]::IsPathRooted($CandidateReceipt)) { $CandidateReceipt } else { Join-Path $repo $CandidateReceipt }
$verified=Get-Content -LiteralPath $candidateReceiptPath -Raw | ConvertFrom-Json
foreach($property in $verified.source_manifest.PSObject.Properties) {
  if((Hash (Join-Path $repo $property.Name)) -ne $property.Value) { throw "Source changed since qualified build: $($property.Name)" }
}
$core=Join-Path $repo 'build\combat-lab\src\core\Release\DSVR.dll'
$loader=Join-Path $repo 'build\src\loader\Release\dinput8.dll'
foreach($path in @($core,$loader)) {
  $relative=(RelPath $repo $path).Replace('\','/')
  if((Hash $path) -ne $verified.binaries_and_package.$relative) { throw "Candidate hash differs from qualified build: $relative" }
}
$syntheticLayer=$null
$simulatorLock=$null
if($InputMode -eq 'SyntheticHmd') {
  $syntheticLayerDir=if([IO.Path]::IsPathRooted($HmdLayerDir)) { [IO.Path]::GetFullPath($HmdLayerDir) } else { [IO.Path]::GetFullPath((Join-Path $repo $HmdLayerDir)) }
  $buildRoot=[IO.Path]::GetFullPath((Join-Path $repo 'build'))+[IO.Path]::DirectorySeparatorChar
  if(-not $syntheticLayerDir.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Synthetic HMD layer must be built under the repository build directory.' }
  $layerDll=Join-Path $syntheticLayerDir 'DSVR-HmdLayer.dll'
  $layerManifest=Join-Path $syntheticLayerDir 'DSVR_hmd_lab.json'
  foreach($path in @($layerDll,$layerManifest)) { if(-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing synthetic HMD layer artifact: $path" } }
  $manifest=Get-Content -LiteralPath $layerManifest -Raw | ConvertFrom-Json
  if($manifest.api_layer.name -ne 'XR_APILAYER_DSVR_hmd_lab' -or $manifest.api_layer.library_path -ne './DSVR-HmdLayer.dll') { throw 'Synthetic HMD layer manifest identity is unexpected.' }
  $activeRuntime=(Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime
  if([IO.Path]::GetFileName($activeRuntime) -ine 'steamxr_win64.json') { throw 'SyntheticHmd mode requires SteamVR as the unchanged active OpenXR runtime.' }
  $script:runtimeRoot=RuntimeRootFromActiveRuntime $activeRuntime
  $steamRoot=[IO.Path]::GetFullPath((Join-Path $script:runtimeRoot '..\..\..'))
  $script:steamVrSettingsPath=Join-Path $steamRoot 'config\steamvr.vrsettings'
  if(-not (Test-Path -LiteralPath $script:steamVrSettingsPath -PathType Leaf)) { throw 'SteamVR settings file is missing.' }
  if(@(SteamVrProcesses).Count) { throw 'SteamVR is already running; SyntheticHmd mode will not change settings beneath a live runtime.' }
  Get-Content -LiteralPath $script:steamVrSettingsPath -Raw | ConvertFrom-Json -AsHashtable | Out-Null
  $syntheticLayer=[ordered]@{directory=$syntheticLayerDir;dll_sha256=(Hash $layerDll);manifest_sha256=(Hash $layerManifest);active_runtime=$activeRuntime;settings_path=$script:steamVrSettingsPath;settings_baseline_sha256=(Hash $script:steamVrSettingsPath);scenario='game-smoke-delay900-v1'}
  if(-not $Cases -or @((ParseCases $Cases) | Where-Object { $_.label -notlike 'synthetic_*' }).Count) { throw 'SyntheticHmd mode requires only synthetic_* case labels.' }
} elseif($InputMode -eq 'OpenXRSimulator') {
  $syntheticLayerDir=if([IO.Path]::IsPathRooted($HmdLayerDir)) { [IO.Path]::GetFullPath($HmdLayerDir) } else { [IO.Path]::GetFullPath((Join-Path $repo $HmdLayerDir)) }
  $buildRoot=[IO.Path]::GetFullPath((Join-Path $repo 'build'))+[IO.Path]::DirectorySeparatorChar
  if(-not $syntheticLayerDir.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'DSVR HMD telemetry layer must be built under the repository build directory.' }
  $layerDll=Join-Path $syntheticLayerDir 'DSVR-HmdLayer.dll'
  $layerManifest=Join-Path $syntheticLayerDir 'DSVR_hmd_lab.json'
  foreach($path in @($layerDll,$layerManifest)) { if(-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing DSVR HMD telemetry-layer artifact: $path" } }
  $manifest=Get-Content -LiteralPath $layerManifest -Raw | ConvertFrom-Json
  if($manifest.api_layer.name -ne 'XR_APILAYER_DSVR_hmd_lab' -or $manifest.api_layer.library_path -ne './DSVR-HmdLayer.dll') { throw 'DSVR HMD layer manifest identity is unexpected.' }
  $simulatorLock=OpenXRSimulatorLock
  if(Get-Process -Name DarkSoulsRemastered -ErrorAction SilentlyContinue) { throw 'DSR must be closed before OpenXR-Simulator preflight.' }
  $script:systemRuntimeBefore=(Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime
  if(-not $script:systemRuntimeBefore -or -not (Test-Path -LiteralPath $script:systemRuntimeBefore -PathType Leaf)) { throw 'Current system OpenXR runtime registration is missing or invalid.' }
  if(Get-Process -Name vrserver,vrcompositor,vrmonitor,vrstartup -ErrorAction SilentlyContinue) { throw 'A SteamVR session is already active; this runner will not take over or stop it.' }
  $syntheticLayer=[ordered]@{directory=$syntheticLayerDir;dll_sha256=(Hash $layerDll);manifest_sha256=(Hash $layerManifest);
    scenario='game-smoke-simulator-v1';runtime_manifest=$simulatorLock.runtime_manifest;
    runtime_manifest_sha256=$simulatorLock.runtime_manifest_sha256;runtime_dll=$simulatorLock.runtime_dll;
    runtime_dll_sha256=$simulatorLock.runtime_dll_sha256;source_commit=$simulatorLock.source_commit;
    mcp_server=$simulatorLock.mcp_server;input_source='upstream MCP ray actions and simulator-native keyboard controls';system_runtime_before=$script:systemRuntimeBefore}
}
$seeds=@((Join-Path $SeedVault 'test-root-0'),(Join-Path $SeedVault 'test-root-1'))
if(-not (Test-Path -LiteralPath (Join-Path $seeds[0] "$account\DRAKS0005.sl2"))) { throw 'Disposable active-account seed missing.' }
foreach($path in @($saveRoots+$seeds)) { if(-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Missing root: $path" }; @(Inventory $path) | Out-Null }
$steamData=Join-Path $steam "userdata\$account\570940"
@(Inventory $steamData) | Out-Null
foreach($name in $installNames) { Hash (Join-Path $game $name) | Out-Null }
if(Test-Path -LiteralPath $Receipt) { throw 'Receipt already exists; use Restore or a new run ID.' }
$plan=ParseCases $Cases
if($InputMode -eq 'OpenXRSimulator' -and -not $Cases) {
  $remaining=[int]$SimulatorSoakSeconds+[int]$MenuBudgetSeconds
  if($remaining -eq 0) { $plan=@([pscustomobject]@{label='simulator_hmd';seconds=60}) }
  else {
    $index=1
    while($remaining -gt 0) {
      $window=[math]::Min(600,$remaining)
      $plan+=@([pscustomobject]@{label="simulator_hmd_$index";seconds=$window})
      $remaining-=$window; $index++
    }
  }
}
if($plan.Count) {
  $total=[int](($plan | Measure-Object -Property seconds -Sum).Sum)
  if($total -gt $WindowMinutes*60) { throw "Case plan needs $total s; the observer window is $($WindowMinutes*60) s." }
  Write-Output "Case plan: $($plan.Count) declared windows totalling $total s."
}
Write-Output "CHECK PASS: pinned candidate, offline/disconnected Steam, exact-game block, disposable seeds and original roots. Input mode: $InputMode. No changes yet."
if($Mode -eq 'Check') { exit 0 }
if($InputMode -eq 'SyntheticHmd') {
  $script:hmdMutex=[Threading.Mutex]::new($false,'Local\DSVR-HmdLab')
  $acquired=$false
  try { $acquired=$script:hmdMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $acquired=$true }
  if(-not $acquired) { $script:hmdMutex.Dispose(); $script:hmdMutex=$null; throw 'Another standalone or protected HMD test run is active.' }
}
[IO.Directory]::CreateDirectory($evidence) | Out-Null
$CasesPath=Join-Path $evidence 'combat-cases.json'
[IO.Directory]::CreateDirectory($transactionParent) | Out-Null
$script:transactionLease=[IO.File]::Open((Join-Path $transactionParent 'active-session.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$vault=Join-Path $transactionParent ('combat-observer-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $vault | Out-Null
$steamConfig=Join-Path $steam "userdata\$account\config\localconfig.vdf"
$script:r=[ordered]@{state='Preparing';vault=$vault;started=(Get-Date).ToString('o');deadline=(Get-Date).AddMinutes($WindowMinutes).ToString('o');pids=@();runtime_processes=@();runtime_processes_before=@();runtime_peak_private_mb=@{};runtime_cleanup_ok=$null;runtime_forced_pids=@();trace_stop_pids=@();trace_stop_token=[guid]::NewGuid().ToString('N');roots=@();files=@();evidence=$evidence;steam_data=$steamData;steam_config=$steamConfig;steam_before=@();traces_before=@((Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-*.jsonl').Name);input_mode=$InputMode;synthetic_layer=$syntheticLayer;simulator_control=@();simulator_server_processes=@();simulator_control_forced=$false;runtime_registration_unchanged=$null;memory_samples=0;memory_peak_private_mb=@{};notes=if($InputMode -eq 'SyntheticHmd') {'Legacy SteamVR null-driver synthetic OpenXR route retained for historical receipts; not used by the selected OpenXR-Simulator runner. No physical-hit claim.'} elseif($InputMode -eq 'OpenXRSimulator') {'Unmodified upstream OpenXR Simulator v1.7.1 selected with child-process XR_RUNTIME_JSON. MCP screenshot-based controller-ray actions and the simulator documented keyboard map drive only supported controls; per-process SIMXR_DATA_DIR and Background input polling are isolated to this receipt. System runtime registration is preserved. Steam stays Offline with the game in Offline Mode (no firewall rules); disposable save/install transactions remain enforced. No physical-hit claim.'} else {'Observer-only LAB core; no XR/input/gameplay writes. Steam metadata snapshotted, live cache not overwritten. Keep Steam Offline. Cloud state is not independently proven; Steam Offline plus in-game Offline Mode remain required.'}}
if($InputMode -eq 'OpenXRSimulator') {
  $script:simulatorRuntimeLock=$simulatorLock
  $script:simulatorDataDir=Join-Path $evidence 'simxr-data'
  [IO.Directory]::CreateDirectory($script:simulatorDataDir) | Out-Null
  [IO.File]::WriteAllText((Join-Path $script:simulatorDataDir 'settings.json'),'{"input_polling":"background"}',[Text.Encoding]::ASCII)
  $script:r.simulator=[ordered]@{
    input_polling='background'
    runtime_name='OpenXR Simulator';runtime_version='1.7.1';source_commit=$simulatorLock.source_commit
    runtime_manifest=$simulatorLock.runtime_manifest;runtime_manifest_sha256=$simulatorLock.runtime_manifest_sha256
    runtime_dll=$simulatorLock.runtime_dll;runtime_dll_sha256=$simulatorLock.runtime_dll_sha256
    mcp_server=$simulatorLock.mcp_server;data_dir=$script:simulatorDataDir;isolated_data_dir=$true
    input_source='upstream MCP ray actions and simulator-native keyboard controls'
    system_runtime_before=$script:systemRuntimeBefore;process_override_used=$true
    runtime_module_loaded=$false;layer_module_loaded=$false
    }
}
SaveReceipt
try {
  $r.candidate_receipt=$candidateReceiptPath
  $r.candidate_receipt_sha256=Hash $candidateReceiptPath
  $r.candidate_binaries=[ordered]@{core=(Hash $core);loader=(Hash $loader)}
  $r.case_plan=$Cases
  $r.case_labels=@($plan | ForEach-Object { $_.label })
  $r.cases_file=$CasesPath
  SaveReceipt
  $r.steam_before=@(CopyTree $steamData (Join-Path $vault 'steam-userdata-before')); SaveReceipt
  if(Test-Path -LiteralPath $steamConfig) { Copy-Item -LiteralPath $steamConfig -Destination (Join-Path $vault 'steam-localconfig-before.vdf') }
  for($i=0;$i -lt 2;++$i) {
    $r.roots+=@([pscustomobject]@{original=$saveRoots[$i];held=(Join-Path $vault "personal-root-$i");testOutput=(Join-Path $vault "test-root-$i");seed=$seeds[$i];inventory=@(Inventory $saveRoots[$i]);moved=$false})
  }
  foreach($name in $installNames) {
    $path=Join-Path $game $name; $backup=Join-Path $vault $name
    Copy-Item -LiteralPath $path -Destination $backup
    $hash=Hash $path
    if((Hash $backup) -ne $hash) { throw 'Backup hash mismatch.' }
    $r.files+=@([pscustomobject]@{path=$path;backup=$backup;originalHash=$hash;expectedHash=$hash})
  }
  SaveReceipt
  StageRoots $r.roots
  $configName=if($InputMode -eq 'OpenXRSimulator') { 'openxr-simulator-candidate.toml' } else { 'observer-candidate.toml' }
  $config=Join-Path $vault $configName
  $configContent=if($InputMode -eq 'OpenXRSimulator') { OpenXRSimulatorConfig } else { ObserverConfig }
  [IO.File]::WriteAllText($config,$configContent,[Text.UTF8Encoding]::new($false))
  StageFile $r.files[0] $loader; StageFile $r.files[1] $core; StageFile $r.files[2] $config
  if($InputMode -eq 'SyntheticHmd') {
    $r.runtime_processes_before=@(SteamVrProcesses | ForEach-Object { @{id=$_.Id;path=$_.Path;started_utc=$_.StartTime.ToUniversalTime().ToString('o')} })
    if($r.runtime_processes_before.Count) { throw 'SteamVR started during preparation; refusing to change settings beneath it.' }
    SaveReceipt
    ApplySteamVrSettings
  }
  $r.state='ObserverReady'; SaveReceipt
  # Generation-adoption floor: any game created before this instant came from
  # someone else's launch, so TrackGames must refuse it (see fixtures).
  $r.launch_requested=(Get-Date).ToString('o'); SaveReceipt
  Write-Output "Launching protected game session in $InputMode mode; deadline $($r.deadline). Combat recording starts automatically for ten minutes; no F9 required."
  Write-Output "After the declared work, the runner will request a trace footer and wait for normal game exit. To finish early: pwsh -NoProfile -File `"$PSCommandPath`" -Mode Finish -Receipt `"$Receipt`"."
  if($InputMode -eq 'SyntheticHmd') {
    $environmentNames=@('XR_API_LAYER_PATH','XR_ENABLE_API_LAYERS','DSVR_HMD_LAB','DSVR_HMD_LAB_DIR','DSVR_HMD_LAB_SCENARIO','SteamAppId','SteamGameId')
    $environmentBefore=@{}
    foreach($name in $environmentNames) { $environmentBefore[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
    try {
      [Environment]::SetEnvironmentVariable('XR_API_LAYER_PATH',$syntheticLayer.directory,'Process')
      [Environment]::SetEnvironmentVariable('XR_ENABLE_API_LAYERS','XR_APILAYER_DSVR_hmd_lab','Process')
      [Environment]::SetEnvironmentVariable('DSVR_HMD_LAB','1','Process')
      [Environment]::SetEnvironmentVariable('DSVR_HMD_LAB_DIR',$evidence,'Process')
      [Environment]::SetEnvironmentVariable('DSVR_HMD_LAB_SCENARIO','game-smoke','Process')
      [Environment]::SetEnvironmentVariable('SteamAppId','570940','Process')
      [Environment]::SetEnvironmentVariable('SteamGameId','570940','Process')
      Start-Process -FilePath (Join-Path $game 'DarkSoulsRemastered.exe') -WorkingDirectory $game | Out-Null
    } finally {
      foreach($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$environmentBefore[$name],'Process') }
    }
  } elseif($InputMode -eq 'OpenXRSimulator') {
    $environmentNames=@('XR_RUNTIME_JSON','XR_API_LAYER_PATH','XR_ENABLE_API_LAYERS','DSVR_HMD_LAB','DSVR_HMD_LAB_DIR',
      'DSVR_HMD_LAB_SCENARIO','SIMXR_DATA_DIR','SteamAppId','SteamGameId')
    $environmentBefore=@{}
    foreach($name in $environmentNames) { $environmentBefore[$name]=[Environment]::GetEnvironmentVariable($name,'Process') }
    try {
      [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON',$simulatorLock.runtime_manifest,'Process')
      [Environment]::SetEnvironmentVariable('XR_API_LAYER_PATH',$syntheticLayer.directory,'Process')
      [Environment]::SetEnvironmentVariable('XR_ENABLE_API_LAYERS','XR_APILAYER_DSVR_hmd_lab','Process')
      [Environment]::SetEnvironmentVariable('DSVR_HMD_LAB','1','Process')
      [Environment]::SetEnvironmentVariable('DSVR_HMD_LAB_DIR',$evidence,'Process')
      [Environment]::SetEnvironmentVariable('DSVR_HMD_LAB_SCENARIO','game-smoke-simulator-v1','Process')
      [Environment]::SetEnvironmentVariable('SIMXR_DATA_DIR',$script:simulatorDataDir,'Process')
      [Environment]::SetEnvironmentVariable('SteamAppId','570940','Process')
      [Environment]::SetEnvironmentVariable('SteamGameId','570940','Process')
      Start-Process -FilePath (Join-Path $game 'DarkSoulsRemastered.exe') -WorkingDirectory $game | Out-Null
    } finally {
      foreach($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name,$environmentBefore[$name],'Process') }
    }
  } else {
    Start-Process -FilePath (Join-Path $steam 'steam.exe') -ArgumentList '-applaunch','570940' | Out-Null
  }
  WaitForSingleGameLaunch
  if($InputMode -eq 'OpenXRSimulator') { StartOpenXRSimulatorControl }
  WaitForTraceStart
  $tick={
    TrackGames
    if($InputMode -eq 'OpenXRSimulator') {
      SampleOpenXRSimulatorMemory
      if($script:simulatorControlProcess -and $script:simulatorControlProcess.HasExited -and $script:simulatorControlProcess.ExitCode -ne 0) {
        throw "Simulator MCP controller exited with code $($script:simulatorControlProcess.ExitCode)."
      }
    }
  }
  RunCasePlan $plan $tick
  if($InputMode -eq 'OpenXRSimulator') { WaitForOpenXRSimulatorControl }
  WaitForGameSession
} finally {
  # Final exact-identity inventory before restoration; a launch this session
  # did not produce is excluded from ownership and pauses restoration instead.
  try { CompleteTransaction }
  finally {
    if($script:hmdMutex) { $script:hmdMutex.ReleaseMutex(); $script:hmdMutex.Dispose(); $script:hmdMutex=$null }
  }
}
