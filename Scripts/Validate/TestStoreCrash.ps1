param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[switch]$Import)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskRun=Join-Path $taskRoot ('Saved/Automation/V10Crash/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskRun -Force | Out-Null
$taskDb=Join-Path $taskRun 'state.sqlite'
$taskExe=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskResults=@()
foreach($taskPhase in @('Seed','CrashBefore','VerifyBefore','CrashAfter','VerifyAfter')){
 $taskReport=Join-Path $taskRun $taskPhase
 New-Item -ItemType Directory -Path $taskReport -Force | Out-Null
 $taskArgs=@('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"','-unattended','-nop4','-nullrhi','-nosound','-nosplash','-AetherAllowSyntheticStoreCrash',"-AetherStoreProbePhase=$taskPhase",'-AetherStoreProbeDb="'+$taskDb+'"','-ExecCmds="Automation RunTests Aether.CrashProbe.SQLiteProcessRecovery"','-TestExit="Automation Test Queue Empty"','-ReportExportPath="'+$taskReport+'"','-abslog="'+(Join-Path $taskReport 'Engine.log')+'"')
 if($Import){$taskArgs+='-AetherStoreProbeImport'}
 $taskProcess=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
 try {
  if(!$taskProcess.WaitForExit(60000)){throw "Crash probe phase timed out: $taskPhase"}
  $taskExpected=if($taskPhase -eq 'CrashBefore'){91}elseif($taskPhase -eq 'CrashAfter'){92}else{0}
  if($taskProcess.ExitCode -ne $taskExpected){throw "Unexpected exit $($taskProcess.ExitCode), expected $taskExpected in $taskPhase"}
  if($taskExpected -eq 0){
   $taskReportJson=Get-Content -LiteralPath (Join-Path $taskReport 'index.json') -Raw|ConvertFrom-Json
   if($taskReportJson.failed -ne 0 -or $taskReportJson.notRun -ne 0 -or $taskReportJson.succeeded -ne 1){throw "Recovery assertion failed: $taskPhase"}
  }
  $taskResults+=[ordered]@{phase=$taskPhase;exitCode=$taskProcess.ExitCode;passed=$true;log=(Join-Path $taskReport 'Engine.log')}
  Write-Output "Process recovery PASS: $taskPhase"
 } finally {if(!$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
}
[ordered]@{schema=1;scenario=$(if($Import){'legacy_import'}else{'normal_commit'});database=$taskDb;phases=$taskResults;boundary='Forced process exit before and after SQLite COMMIT; not a power-loss or hardware-damage test'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $taskRun 'result.json') -Encoding utf8
Write-Output "Crash recovery PASS: $taskRun"
