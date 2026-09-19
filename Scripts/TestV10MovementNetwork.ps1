param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$Port=7813)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskProject='"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'
$taskPrefix='AetherV10Movement_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskProcesses=[System.Collections.Generic.List[object]]::new()
$taskLogs=[System.Collections.Generic.List[string]]::new()
$taskStarted=Get-Date
function Start-ClosureProcess([string]$Name,[string[]]$Extra){
 $taskLog=Join-Path $taskRoot "Saved/Logs/$taskPrefix-$Name.log"
 $taskLogs.Add($taskLog)
 $taskProc=Start-Process -FilePath $taskEditor -ArgumentList (@($taskProject)+$Extra+@('-AetherV10MovementNetwork','-AetherKeepRegionsLoaded','-nullrhi','-nosound','-unattended','-nop4','-ExecCmds="t.MaxFPS 30"',"-abslog=$taskLog")) -PassThru -WindowStyle Hidden
 $taskProcesses.Add($taskProc)
 return @{Process=$taskProc;Log=$taskLog}
}
function Wait-ClosureMarker($Task,[string]$Pattern,[int]$Seconds=60){
 $taskDeadline=(Get-Date).AddSeconds($Seconds)
 while(!(Test-Path -LiteralPath $Task.Log) -or !(Select-String -LiteralPath $Task.Log -Pattern $Pattern -Quiet)){
  if($Task.Process.HasExited -or (Get-Date) -gt $taskDeadline){throw "Missing $Pattern in $($Task.Log)"}
  Start-Sleep -Milliseconds 500
 }
}
function Wait-ClosureExit($Task,[int]$Seconds=60){
 $taskDeadline=(Get-Date).AddSeconds($Seconds)
 while(!$Task.Process.HasExited){if((Get-Date) -gt $taskDeadline){throw "Process timeout: $($Task.Log)"};Start-Sleep -Milliseconds 500}
 $Task.Process.WaitForExit()
 if($Task.Process.ExitCode -ne 0){throw "Exit $($Task.Process.ExitCode): $($Task.Log)"}
}
function Start-ClosureClient([string]$Name,[string]$Profile){return Start-ClosureProcess $Name @("127.0.0.1:$Port`?DevProfile=$Profile",'-game','-AetherV807Client')}
# 每次生成独立存档前缀，只驱动本脚本创建的服务器与客户端。
try {
 $taskServer=Start-ClosureProcess 'Server' @('/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode','-server','-game','-AetherV807Server',"-AetherSavePrefix=$taskPrefix","-port=$Port")
 Wait-ClosureMarker $taskServer 'AETHER_V4_READY'
 $taskAlpha=Start-ClosureClient 'Alpha' 'Alpha'
 $taskBeta=Start-ClosureClient 'Beta' 'Beta'
 Wait-ClosureExit $taskServer 80
 if(!(Select-String -LiteralPath $taskServer.Log -Pattern 'V10_MOVEMENT_NETWORK_PASS' -Quiet)){throw 'Movement network check failed.'}
 foreach($taskLog in $taskLogs){
  if(Select-String -LiteralPath $taskLog -Pattern 'V807_CASE FAIL|V807_CLIENT_ACK FAIL|V10_MOVEMENT_CLIENT_FAIL|V10_MOVEMENT_NETWORK_FAIL|V807_FAIL|Fatal error:' -Quiet){throw "Failure: $taskLog"}
 }
 # 逐一要求每个客户端的七个阶段通过，不能只相信服务器汇总日志。
 foreach($taskClient in @($taskAlpha,$taskBeta)){
  foreach($taskPhase in 1..7){
   if(!(Select-String -LiteralPath $taskClient.Log -Pattern "V10_MOVEMENT_CLIENT_PASS id=\w+ phase=$taskPhase " -Quiet)){throw "Missing phase $taskPhase in $($taskClient.Log)"}
  }
 }
 $taskResult=[ordered]@{
  FixtureVersion=10;BaseCommitSha=(& git -c "safe.directory=$taskRoot" -C $taskRoot rev-parse HEAD)
  SourceFingerprint=(& (Join-Path $PSScriptRoot 'GetV9SourceFingerprint.ps1'))
  Engine='UE 5.8';Target='Editor Development dedicated process + 2 remote clients'
  Started=$taskStarted.ToString('o');Finished=(Get-Date).ToString('o');SavePrefix=$taskPrefix
  Passed=@('Real crouch capsules and remote stance replication','SavedMove sprint input and authoritative stamina cost','Local server and remote observed jumps','Input flush stops held actions','Predicted grounded dodge and single stamina charge','Server rejection rolls back predicted stamina and movement','Owner-only profile state')
  NotRun=@('Listen mode','Loss/latency injection','Four players','Streaming unload','Restart recovery','Animation quality','Shipping package');Logs=@($taskLogs)
 }
 $taskResult | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRoot 'Saved/Automation/V10-MovementNetwork-Result.json') -Encoding utf8
 Write-Output "V10 movement dedicated two-client check PASS; logs $taskPrefix"
} finally {
 foreach($taskProc in $taskProcesses){if(!$taskProc.HasExited){Stop-Process -Id $taskProc.Id}}
}
