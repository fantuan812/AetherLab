param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'AetherLab.uproject'
$logPath = Join-Path $projectRoot 'Saved\Logs\AetherSmoke.log'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $logPath) | Out-Null
& $editorCmd $projectPath "/Engine/Maps/Entry?game=/Script/AetherLab.ReactiveLabGameMode" -game -AetherSmoke -unattended -nop4 -nullrhi -nosound -nosplash "-abslog=$logPath" -stdout -FullStdOutLogOutput
$engineExitCode = $LASTEXITCODE
if ($engineExitCode -ne 0) { exit $engineExitCode }
if (!(Select-String -LiteralPath $logPath -Pattern 'AETHER_SMOKE_PASS' -Quiet)) { throw 'Runtime smoke check did not pass.' }
if (Select-String -LiteralPath $logPath -Pattern 'Log[A-Za-z0-9_]+: Error:' -Quiet) { throw 'The runtime emitted an error; inspect the smoke log.' }
Write-Output 'Aether runtime smoke check passed.'
