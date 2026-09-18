param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [string]$Filter='Aether.V10.+Aether.V9.+Aether.V802.'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskReport=Join-Path $taskRoot ('Saved/Automation/Rules-'+[guid]::NewGuid().ToString('N'))
$taskLog=Join-Path $taskReport 'Engine.log'
New-Item -ItemType Directory -Path $taskReport -Force | Out-Null
$taskExe=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $taskExe (Join-Path $taskRoot 'AetherLab.uproject') -unattended -nop4 -nullrhi -nosound -nosplash "-ExecCmds=Automation RunTests $Filter" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskReport" "-abslog=$taskLog"
if($LASTEXITCODE -ne 0){throw "Editor failed: $taskLog"}
# TestExit=队列为空会在用例失败时仍返回 0，必须验证新目录内的报告，而非进程状态。
$taskResult=Get-Content -LiteralPath (Join-Path $taskReport 'index.json') -Raw | ConvertFrom-Json
if($null -eq $taskResult.failed -or $taskResult.failed -ne 0 -or $taskResult.notRun -ne 0 -or $taskResult.inProcess -ne 0 -or ($taskResult.succeeded+$taskResult.succeededWithWarnings) -lt 1) {
 throw "Rules failed or incomplete: $taskReport"
}
Write-Output ("Rules PASS: "+($taskResult.succeeded+$taskResult.succeededWithWarnings)+"; report "+$taskReport)
