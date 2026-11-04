#requires -Version 7.0
[CmdletBinding()]
param(
  [string]$RunDir,
  [string]$BuildDir='build',
  [ValidateSet('Debug','Release')][string]$Configuration='Release',
  [switch]$RuntimeOnly,
  [switch]$SelfTest
)
$arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot 'run_openxr_simulator_hmd.ps1'),'-BuildDir',$BuildDir,'-Configuration',$Configuration)
if($RunDir) { $arguments+=@('-RunDir',$RunDir) }
if($RuntimeOnly) { $arguments+='-RuntimeOnly' }
if($SelfTest) { $arguments+='-SelfTest' }
& pwsh @arguments
exit $LASTEXITCODE
