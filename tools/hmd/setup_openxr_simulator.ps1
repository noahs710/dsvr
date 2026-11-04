#requires -Version 7.0
[CmdletBinding()]
param([switch]$SkipBuild, [string]$Python='python')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$research=Join-Path $repo 'build/research'
$source=Join-Path $research 'openxr-simulator-upstream'
$build=Join-Path $research 'openxr-simulator-upstream-build'
$venv=Join-Path $research 'openxr-simulator-upstream-python'
$pythonExe=Join-Path $venv 'Scripts/python.exe'
$commit='692a66de570189b29c684f48c6ef306a07d5764f'
function RequireExit([string]$operation) { if($LASTEXITCODE -ne 0) { throw "$operation failed ($LASTEXITCODE)." } }
if(-not (Test-Path (Join-Path $source '.git'))) {
  if(Test-Path $source) { throw 'Simulator source directory exists but is not a Git checkout.' }
  git clone --no-checkout https://github.com/elliotttate/OpenXR-Simulator.git $source
  RequireExit 'Simulator clone'
  git -C $source checkout --detach $commit
  RequireExit 'Pinned simulator checkout'
}
$head=git -C $source rev-parse HEAD
RequireExit 'Simulator source identity'
if($head -ne $commit) { throw "Simulator checkout is not pinned to $commit; existing checkout left untouched." }
$dirty=git -C $source status --porcelain --untracked-files=no
RequireExit 'Simulator source status'
if($dirty) { throw 'Simulator upstream sources are modified; refusing to build a patched runtime.' }
if(-not (Test-Path $pythonExe)) {
  & $Python -m venv $venv
  RequireExit 'Simulator Python environment'
  & $pythonExe -m pip install -e (Join-Path $source 'mcp-server')
  RequireExit 'Upstream MCP installation'
}
& $pythonExe -m pip check
RequireExit 'Simulator dependency consistency'
& $pythonExe -B -c 'import mcp, numpy, PIL, ui_targeting'
RequireExit 'Simulator MCP imports'
if(-not $SkipBuild) {
  $vulkan=Join-Path $research 'Vulkan-Headers/include'
  if(-not (Test-Path (Join-Path $vulkan 'vulkan/vulkan.h'))) { throw 'Vulkan headers missing: install the Vulkan SDK or provision build/research/Vulkan-Headers.' }
  cmake -S $source -B $build -G 'Visual Studio 18 2026' -A x64 "-DSIMXR_VULKAN_INCLUDE_DIR=$vulkan" -DSIMXR_ENABLE_UI_AUTOMATION_TESTS=ON "-DPython3_EXECUTABLE=$pythonExe"
  RequireExit 'Simulator configure'
  cmake --build $build --config Release --parallel
  RequireExit 'Simulator build'
}
$runtime=Join-Path $source 'bin/openxr_simulator.dll'
$manifest=Join-Path $source 'bin/openxr_simulator.json'
$server=Join-Path $source 'mcp-server/openxr_simulator_mcp.py'
$viewer=Join-Path $source 'bin/openxr_sim_frametime.exe'
foreach($path in @($runtime,$manifest,$server,$viewer,(Join-Path $build 'CTestTestfile.cmake'))) {
  if(-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Simulator artifact missing: $path" }
}
$hashes=[ordered]@{}
foreach($path in @($runtime,$manifest,$server,$viewer)) { $hashes[$path]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
$dependencies=@(& $pythonExe -m pip freeze)
RequireExit 'Simulator dependency snapshot'
$lock=[ordered]@{schema=2;source_url='https://github.com/elliotttate/OpenXR-Simulator';source_commit=$commit;upstream_unmodified=$true;runtime_manifest=$manifest;runtime_manifest_sha256=$hashes[$manifest];runtime_dll=$runtime;runtime_dll_sha256=$hashes[$runtime];mcp_python=$pythonExe;mcp_server=$server;mcp_server_sha256=$hashes[$server];viewer=$viewer;viewer_sha256=$hashes[$viewer];requirements_sha256=(Get-FileHash -LiteralPath (Join-Path $source 'mcp-server/requirements.txt') -Algorithm SHA256).Hash.ToLowerInvariant();build_dir=$build;artifacts=$hashes;dependencies=$dependencies;ui_automation_enabled=$true}
$lock | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $build 'upstream-lock.json') -Encoding utf8NoBOM
Write-Output "Official OpenXR Simulator ready: $commit; real MCP/UI automation enabled."
