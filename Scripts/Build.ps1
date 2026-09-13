param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'AetherLab.uproject'
$buildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (!(Test-Path -LiteralPath $buildTool)) { throw "UE build tool not found: $buildTool" }
& $buildTool AetherLabEditor Win64 Development $projectPath -WaitMutex -NoHotReloadFromIDE -NoUBA
exit $LASTEXITCODE
