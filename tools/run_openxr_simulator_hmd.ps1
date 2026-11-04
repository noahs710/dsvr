#requires -Version 7.0
[CmdletBinding()]
param(
  [string]$RunDir,
  [string]$BuildDir='build',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [switch]$RuntimeOnly,
  [switch]$SelfTest
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$setup=Join-Path $PSScriptRoot 'hmd/setup_openxr_simulator.ps1'
if($SelfTest) {
  if(-not (Test-Path $setup)) { throw 'Upstream simulator setup script missing.' }
  [Management.Automation.Language.Parser]::ParseFile($setup,[ref]$null,[ref]$null) | Out-Null
  Write-Output 'Official simulator runner self-test PASS; no runtime or desktop changes.'
  exit 0
}
if(Get-Process -Name DarkSoulsRemastered,DSVR-HmdLab,vrserver,vrcompositor -ErrorAction SilentlyContinue) { throw 'An XR/game session is already active; simulator verification will not interfere.' }
$before=(Get-ItemProperty 'HKLM:/SOFTWARE/Khronos/OpenXR/1').ActiveRuntime
$verify=Join-Path $repo 'build/verification'
$run=if($RunDir) { [IO.Path]::GetFullPath($RunDir) } else { Join-Path $verify ('upstream-simulator-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)) }
if(-not $run.StartsWith([IO.Path]::GetFullPath($verify)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'RunDir must stay under build/verification.' }
if(Test-Path $run) { throw 'RunDir already exists; earlier evidence is immutable.' }
[IO.Directory]::CreateDirectory($run) | Out-Null
$receipt=[ordered]@{schema=2;state='running';started_utc=[datetime]::UtcNow.ToString('o');scope='Official upstream live XR probes plus DSVR build/test; simulator and DSVR results are reported independently';runtime_registration_before=$before;simulator_exit=$null;dsvr_build_exit=$null;dsvr_test_exit=$null;runtime_registration_unchanged=$false;human_input=$false}
$exit=1
try {
  & $setup -SkipBuild:$RuntimeOnly
  if(-not $?) { throw 'Official simulator setup failed.' }
  $simBuild=Join-Path $repo 'build/research/openxr-simulator-upstream-build'
  $receipt.simulator_lock=Join-Path $simBuild 'upstream-lock.json'
  ctest --test-dir $simBuild -C Release --output-on-failure --output-junit (Join-Path $run 'simulator-tests.xml') 2>&1 | Tee-Object -FilePath (Join-Path $run 'simulator-ctest.log')
  $receipt.simulator_exit=$LASTEXITCODE
  if($LASTEXITCODE -ne 0) { $receipt.simulator_error='Official UI test process failed; inspect per-case interaction result and teardown log.' }
  $junit=[xml](Get-Content -LiteralPath (Join-Path $run 'simulator-tests.xml') -Raw)
  $suite=if($junit.testsuite) { $junit.testsuite } else { $junit.testsuites.testsuite }
  $receipt.simulator_test_total=[int]$suite.tests
  $receipt.simulator_test_failures=[int]$suite.failures
  $receipt.simulator_evidence=Join-Path $simBuild 'ui-evidence'
  $recentUi=@(Get-ChildItem -LiteralPath $receipt.simulator_evidence -Filter 'result.json' -File -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTimeUtc -ge ([datetime]$receipt.started_utc).ToUniversalTime() })
  $receipt.ui_interaction_reports=$recentUi.Count
  $uiPass=0
  foreach($resultFile in $recentUi) { try { if((Get-Content -LiteralPath $resultFile.FullName -Raw | ConvertFrom-Json).passed) { $uiPass++ } } catch { } }
  $receipt.ui_interaction_passed=$uiPass
  if(-not $RuntimeOnly) {
    cmake --build $BuildDir --config $Configuration --parallel 2>&1 | Tee-Object -FilePath (Join-Path $run 'dsvr-build.log')
    $receipt.dsvr_build_exit=$LASTEXITCODE
    if($LASTEXITCODE -ne 0) { throw 'DSVR candidate build failed.' }
    ctest --test-dir $BuildDir -C $Configuration --parallel --output-on-failure --output-junit (Join-Path $run 'dsvr-tests.xml') 2>&1 | Tee-Object -FilePath (Join-Path $run 'dsvr-ctest.log')
    $receipt.dsvr_test_exit=$LASTEXITCODE
    if($LASTEXITCODE -ne 0) { throw 'DSVR candidate tests failed.' }
  }
  if($receipt.simulator_exit -ne 0) {
      $receipt.state='failed'
    $receipt.error=$receipt.simulator_error
    $exit=1
  } else {
    $receipt.state='complete'
    $exit=0
  }
} catch {
  $receipt.state='failed'
  $receipt.error=$_.Exception.Message
  Write-Output $receipt.error
} finally {
  $after=(Get-ItemProperty 'HKLM:/SOFTWARE/Khronos/OpenXR/1').ActiveRuntime
  $receipt.runtime_registration_unchanged=$before -eq $after
  if(-not $receipt.runtime_registration_unchanged) { $receipt.state='failed'; $exit=1 }
  $receipt.finished_utc=[datetime]::UtcNow.ToString('o')
  $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'receipt.json') -Encoding utf8NoBOM
}
Write-Output "Simulator verification evidence: $run"
exit $exit
