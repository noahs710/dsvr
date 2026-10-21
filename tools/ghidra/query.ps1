[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string[]]$Address,
    [string]$ToolRoot = "C:\Tools\DarkSoulsRemaster_De",
    [string]$ProjectDirectory = "analysis-full",
    [string]$ProjectName = "DSRFull",
    [string]$Script = "DumpFunctions.java"
)

$ErrorActionPreference = "Stop"
$javaHome = Join-Path $ToolRoot "jdk-21"
$ghidraHome = Join-Path $ToolRoot "ghidra-12.1.4"
$headless = Join-Path $ghidraHome "support\analyzeHeadless.bat"
$project = Join-Path $ToolRoot $ProjectDirectory

foreach ($path in @($javaHome, $ghidraHome, $headless, $project)) {
    if (!(Test-Path $path)) { throw "Missing Ghidra dependency: $path" }
}

$env:JAVA_HOME = $javaHome
$env:GHIDRA_INSTALL_DIR = $ghidraHome
$env:GHIDRA_HEADLESS_MAXMEM = "32G"

& $headless $project $ProjectName -readOnly -noanalysis `
    -process "DarkSoulsRemastered.exe" `
    -scriptPath $PSScriptRoot `
    -postScript $Script @Address
exit $LASTEXITCODE
