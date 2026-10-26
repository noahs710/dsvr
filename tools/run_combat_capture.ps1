#requires -Version 7.0
# Desktop natural-combat observation only. No input injection, XR or game writes.
# Files/save roots are changed only inside a receipted, reversible transaction.
# Recovered from build/desktop-observer-session.ps1; do not use its old observer
# deployment (duplicate observer_only and regular, non-LAB core).
param(
  [ValidateSet('Check','Run','Restore','SelfTest')][string]$Mode='Check',
  [string]$Receipt,
  [string]$CandidateReceipt='build\verification\combat-observability-20261024\candidate-final-receipt.json',
  # Setup is part of the fixed session deadline. The operator-approved longer
  # retry needs 30 minutes; retain the 15-minute default and a finite upper bound.
  [ValidateRange(1,30)][int]$WindowMinutes=15,
  [string]$Cases,
  [string]$SeedVault=(Join-Path $env:USERPROFILE '.dsvr-backups\desktop-observer-20261023-140650')
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
function Hash($path) {
  (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function RelPath($root,$full) {
  [IO.Path]::GetRelativePath([IO.Path]::GetFullPath($root),[IO.Path]::GetFullPath($full))
}
function Games { @(Get-Process -Name DarkSoulsRemastered -ErrorAction SilentlyContinue | Where-Object { -not $_.HasExited }) }
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
function TrackGames {
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
function TraceInventory {
  $spans=@()
  foreach($trace in @(Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-*.jsonl' |
                        Where-Object { $_.Name -notin $r.traces_before })) {
    $start=$null;$end=$null
    try {
      $lines=[IO.File]::ReadAllLines($trace.FullName)
      if($lines.Length) {
        $head=$lines[0]|ConvertFrom-Json
        if($head.event -eq 'header') { $start=[long]$head.qpc }
        $last=$lines[$lines.Length-1]|ConvertFrom-Json
        if($last.event -eq 'footer') { $end=[long]$last.qpc }
      }
    } catch { }
    $spans+=,[pscustomobject]@{name=$trace.Name;start=$start;end=$end;closed=$null -ne $end}
  }
  # Plain array output: the comma form would hand back the array as one object and
  # every caller's .Count would read 1 however many captures exist.
  return $spans
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
    Write-Output ("CASE $($entry.label): perform it now for $($entry.seconds) seconds. " +
      'An F9 capture holds 20 s, so open one at the start of the action and, for a longer ' +
      'window, again before it ends - a window no capture overlaps has no evidence at all.')
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
  foreach($tracked in $r.pids) {
    $process=Get-Process -Id $tracked.id -ErrorAction SilentlyContinue
    if($process -and $process.Path -ieq $tracked.path -and $process.StartTime.Ticks -eq ([datetime]$tracked.start).Ticks) {
      Stop-Process -Id $process.Id
      Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
  }
  # Force array shape so a one-process result blocks restoration.
  if(@(Games).Count) { throw 'Untracked game process exists; restoration paused.' }
  AssertFiles $r.files; AssertRoots $r.roots
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
  $r.state='Restored'; SaveReceipt
  Write-Output "Restored and hash-verified original save roots and install files. Receipt: $Receipt"
}
function CompleteTransaction {
  # A tracking refusal must not skip stopping known generations or leave a
  # receipt saying ObserverReady. RestoreTransaction still checks for ANY live
  # game before touching roots/files; trying cleanup does not waive admission.
  $trackingFailure=$null
  try { TrackGames } catch {
    $trackingFailure=$_
    SetReceiptValue 'tracking_error' $_.Exception.Message
  }
  try {
    if($r.roots.Count -eq 2 -and $r.files.Count -eq 3) { RestoreTransaction }
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
      pids=@([pscustomobject]@{id=10;start=$base.ToString('o');path=$path})}
    $script:fixtureRestoreAttempted=$false; $script:fixtureReceiptSaved=$false
    $script:fixtureFilesTouched=$false; $script:fixtureKills=@()
    function TrackGames { throw 'fixture unexpected process' }
    function ValidateReceipt { $script:fixtureRestoreAttempted=$true }
    function Get-Process { [pscustomobject]@{Id=10;StartTime=$base;Path=$path} }
    function Stop-Process { param($Id) $script:fixtureKills+=,$Id }
    function Wait-Process { }
    function Games { @([pscustomobject]@{Id=99}) }
    function AssertFiles { $script:fixtureFilesTouched=$true; throw 'Unsafe file access' }
    function SaveReceipt { $script:fixtureReceiptSaved=$true }
    try { CompleteTransaction } catch { }
    if(-not $script:fixtureRestoreAttempted -or -not $script:fixtureReceiptSaved -or
       $r.state -ne 'RecoveryBlocked' -or $script:fixtureFilesTouched -or
       $r.tracking_error -ne 'fixture unexpected process' -or
       $r.recovery_error -ne 'Untracked game process exists; restoration paused.' -or
       $script:fixtureKills.Count -ne 1 -or $script:fixtureKills[0] -ne 10) {
      throw ('Tracking refusal skipped safe recoverable cleanup: '+($r | ConvertTo-Json -Depth 5 -Compress)+
        ' kills='+($script:fixtureKills -join ',')+' attempted='+$script:fixtureRestoreAttempted+
        ' saved='+$script:fixtureReceiptSaved+' files='+$script:fixtureFilesTouched)
    }
    # The same numeric PID with another creation time must NOT be killed.
    $script:fixtureKills=@()
    function Get-Process { [pscustomobject]@{Id=10;StartTime=$base.AddSeconds(1);Path=$path} }
    try { CompleteTransaction } catch { }
    if($script:fixtureKills.Count -or $script:fixtureFilesTouched) { throw 'Cleanup killed a reused PID or touched active saves.' }
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
$script:FinishPath=$Receipt+'.finish'
$Receipt=[IO.Path]::GetFullPath($Receipt)
$evidence=Split-Path $Receipt -Parent
$allowed=[IO.Path]::GetFullPath((Join-Path $repo 'build\verification'))+[IO.Path]::DirectorySeparatorChar
if(-not $Receipt.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Receipt must be under build/verification.' }
if($Mode -eq 'Restore') {
  [IO.Directory]::CreateDirectory($transactionParent) | Out-Null
  $script:transactionLease=[IO.File]::Open((Join-Path $transactionParent 'active-session.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  $script:r=ReadReceipt
  RestoreTransaction
  exit 0
}
# Entire preflight is read-only. Never infer safety from a prior run's receipt.
if(@(Games).Count) { throw 'Game must be closed.' }
if((Get-ItemProperty 'HKCU:\Software\Valve\Steam\ActiveProcess').ActiveUser -ne $account) { throw 'Active Steam account changed.' }
if(-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Steam must already be running in Offline mode.' }
$login=Get-Content -LiteralPath (Join-Path $steam 'config\loginusers.vdf') -Raw
$user=[regex]::Match($login,'"76561199803506767"\s*\{([^{}]*)\}')
if(-not $user.Success -or $user.Groups[1].Value -notmatch '"WantsOfflineMode"\s*"1"') { throw 'Steam Offline preference is not set.' }
$connection=Get-Content -LiteralPath (Join-Path $steam 'logs\connection_log.txt') -Tail 120
$state=@($connection | Select-String '\[(Logged On|Logged Off|Logging On|Logging Off|Connecting|Connected),') | Select-Object -Last 1
if(-not $state -or $state.Line -notmatch '\[Logged Off,') { throw 'Current Steam client is not observed logged off. Use Steam Offline first.' }
$blocks=@(Get-NetFirewallRule -PolicyStore ActiveStore | Where-Object { $_.Enabled -eq 'True' -and $_.Direction -eq 'Outbound' -and $_.Action -eq 'Block' -and $_.Profile -eq 'Any' } |
  Get-NetFirewallApplicationFilter | Where-Object { $_.Program -ieq (Join-Path $game 'DarkSoulsRemastered.exe') })
if(-not $blocks.Count) { throw 'Exact-game outbound blocking is not verified.' }
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
$seeds=@((Join-Path $SeedVault 'test-root-0'),(Join-Path $SeedVault 'test-root-1'))
if(-not (Test-Path -LiteralPath (Join-Path $seeds[0] "$account\DRAKS0005.sl2"))) { throw 'Disposable active-account seed missing.' }
foreach($path in @($saveRoots+$seeds)) { if(-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Missing root: $path" }; @(Inventory $path) | Out-Null }
$steamData=Join-Path $steam "userdata\$account\570940"
@(Inventory $steamData) | Out-Null
foreach($name in $installNames) { Hash (Join-Path $game $name) | Out-Null }
if(Test-Path -LiteralPath $Receipt) { throw 'Receipt already exists; use Restore or a new run ID.' }
$plan=ParseCases $Cases
if($plan.Count) {
  $total=[int](($plan | Measure-Object -Property seconds -Sum).Sum)
  if($total -gt $WindowMinutes*60) { throw "Case plan needs $total s; the observer window is $($WindowMinutes*60) s." }
  Write-Output "Case plan: $($plan.Count) declared windows totalling $total s."
}
Write-Output 'CHECK PASS: pinned candidate, offline/disconnected Steam, exact-game block, disposable seeds and original roots. No changes yet.'
if($Mode -eq 'Check') { exit 0 }
[IO.Directory]::CreateDirectory($evidence) | Out-Null
$CasesPath=Join-Path $evidence 'combat-cases.json'
[IO.Directory]::CreateDirectory($transactionParent) | Out-Null
$script:transactionLease=[IO.File]::Open((Join-Path $transactionParent 'active-session.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$vault=Join-Path $transactionParent ('combat-observer-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $vault | Out-Null
$steamConfig=Join-Path $steam "userdata\$account\config\localconfig.vdf"
$script:r=[ordered]@{state='Preparing';vault=$vault;started=(Get-Date).ToString('o');deadline=(Get-Date).AddMinutes($WindowMinutes).ToString('o');pids=@();roots=@();files=@();evidence=$evidence;steam_data=$steamData;steam_config=$steamConfig;steam_before=@();traces_before=@((Get-ChildItem -LiteralPath $game -Filter 'DSVR-trace-*.jsonl').Name);notes='Operator controls all gameplay. Observer-only LAB core; no XR/input/gameplay writes. Steam metadata snapshotted, live cache not overwritten. Keep Steam Offline. Cloud state is not independently proven; offline/exact-EXE blocking remain required.'}
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
  $config=Join-Path $vault 'observer-candidate.toml'
  [IO.File]::WriteAllText($config,(ObserverConfig),[Text.UTF8Encoding]::new($false))
  StageFile $r.files[0] $loader; StageFile $r.files[1] $core; StageFile $r.files[2] $config
  $r.state='ObserverReady'; SaveReceipt
  # Generation-adoption floor: any game created before this instant came from
  # someone else's launch, so TrackGames must refuse it (see fixtures).
  $r.launch_requested=(Get-Date).ToString('o'); SaveReceipt
  Write-Output "Launching protected desktop observer; operator input only; deadline $($r.deadline). Combat recording starts automatically for ten minutes; no F9 required."
  Start-Process -FilePath (Join-Path $steam 'steam.exe') -ArgumentList '-applaunch','570940' | Out-Null
  WaitForSingleGameLaunch
  RunCasePlan $plan { TrackGames }
  ReportCaseCoverage
  WaitForGameSession
} finally {
  # Final exact-identity inventory before restoration; a launch this session
  # did not produce is excluded from ownership and pauses restoration instead.
  CompleteTransaction
}
