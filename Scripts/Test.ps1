param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$projectPath = Join-Path $projectRoot 'AetherLab.uproject'
$reportPath = Join-Path $projectRoot 'Saved\Automation\Reactive'
$logPath = Join-Path $projectRoot 'Saved\Logs\ReactiveTests.log'
New-Item -ItemType Directory -Force -Path $reportPath,(Split-Path -Parent $logPath) | Out-Null
$testStartTime = Get-Date
& $editorCmd $projectPath -unattended -nop4 -nullrhi -nosound -nosplash '-ExecCmds=Automation RunTests Reactive.' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportPath" "-abslog=$logPath" -stdout -FullStdOutLogOutput
$engineExitCode = $LASTEXITCODE
if ($engineExitCode -ne 0) { exit $engineExitCode }
$reportFile = Join-Path $reportPath 'index.json'
if (!(Test-Path -LiteralPath $reportFile)) { throw 'No automation report was generated.' }
if ((Get-Item -LiteralPath $reportFile).LastWriteTime -lt $testStartTime) { throw 'Automation report was not refreshed by this run.' }
$report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
if ($report.failed -gt 0 -or $report.succeeded -lt 20) { throw "Unexpected automation result: succeeded=$($report.succeeded), failed=$($report.failed)" }
Write-Output "Reactive tests passed: $($report.succeeded)"
