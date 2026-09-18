param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskReport=Join-Path $taskRoot 'Saved\Automation\V7Reaction'
$taskStart=Get-Date
& $taskEditor (Join-Path $taskRoot 'AetherLab.uproject') -unattended -nop4 -nullrhi -nosound -nosplash '-ExecCmds=Automation RunTests Reactive.V7.' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskReport" "-abslog=$taskRoot\Saved\Logs\V7Reaction.log"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$taskFile=Join-Path $taskReport 'index.json'
if(!(Test-Path -LiteralPath $taskFile) -or (Get-Item -LiteralPath $taskFile).LastWriteTime -lt $taskStart){throw 'No fresh lightweight report'}
$taskResult=Get-Content -LiteralPath $taskFile -Raw | ConvertFrom-Json
if($taskResult.failed -gt 0 -or $taskResult.succeeded -ne 8){throw "Tests: $($taskResult.succeeded) passed, $($taskResult.failed) failed"}
Write-Output "Lightweight checks passed: $($taskResult.succeeded)"
