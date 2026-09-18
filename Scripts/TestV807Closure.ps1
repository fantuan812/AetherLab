param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$Port=7798)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskProject='"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'
$taskPrefix='AetherV807_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskProcesses=[System.Collections.Generic.List[object]]::new()
$taskLogs=[System.Collections.Generic.List[string]]::new()
$taskStarted=Get-Date
function Start-ClosureProcess([string]$Name,[string[]]$Extra){
 $taskLog=Join-Path $taskRoot "Saved/Logs/$taskPrefix-$Name.log"
 $taskLogs.Add($taskLog)
 $taskProc=Start-Process -FilePath $taskEditor -ArgumentList (@($taskProject)+$Extra+@('-AetherKeepRegionsLoaded','-nullrhi','-nosound','-unattended','-nop4','-ExecCmds="t.MaxFPS 30"',"-abslog=$taskLog")) -PassThru -WindowStyle Hidden
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
try {
 $taskServer=Start-ClosureProcess 'Server' @('/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode','-server','-game','-AetherV807Server',"-AetherSavePrefix=$taskPrefix","-port=$Port")
 Wait-ClosureMarker $taskServer 'AETHER_V4_READY'
 $taskAlpha=Start-ClosureClient 'Alpha' 'Alpha'
 Wait-ClosureMarker $taskServer 'V807_LATE_READY'
 $taskBeta=Start-ClosureClient 'Beta' 'Beta'
 Wait-ClosureMarker $taskServer 'V807_RECONNECT_READY'
 Wait-ClosureExit $taskBeta
 $taskRejoin=Start-ClosureClient 'Beta-Rejoin' 'Beta'
 Wait-ClosureExit $taskServer
 Wait-ClosureExit $taskAlpha
 Wait-ClosureExit $taskRejoin
 if(!(Select-String -LiteralPath $taskServer.Log -Pattern 'V807_SESSION_PASS' -Quiet)){throw 'Initial closure session failed.'}
 $taskReload=Start-ClosureProcess 'Server-Restart' @('/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode','-server','-game','-AetherV807Server','-AetherV807Reload',"-AetherSavePrefix=$taskPrefix","-port=$Port")
 Wait-ClosureMarker $taskReload 'AETHER_V4_READY'
 $taskAlphaReload=Start-ClosureClient 'Alpha-Restart' 'Alpha'
 $taskBetaReload=Start-ClosureClient 'Beta-Restart' 'Beta'
 Wait-ClosureExit $taskReload
 # A server exiting can reach clients before the reliable close RPC; stop only our completed check peers below.
 if(!(Select-String -LiteralPath $taskReload.Log -Pattern 'V807_RELOAD_PASS' -Quiet)){throw 'Restart closure failed.'}
 foreach($taskLog in $taskLogs){if(Select-String -LiteralPath $taskLog -Pattern 'V807_CASE FAIL|V807_CLIENT_STATE FAIL|V807_CLIENT_ACK FAIL|Fatal error:' -Quiet){throw "Failure: $taskLog"}}
 foreach($taskClient in @($taskAlpha,$taskBeta,$taskRejoin,$taskAlphaReload,$taskBetaReload)){
  if(!(Select-String -LiteralPath $taskClient.Log -Pattern 'V807_CLIENT_STATE PASS' -Quiet)){throw "No remote state evidence: $($taskClient.Log)"}
 }
 $taskResult=[ordered]@{FixtureVersion=807;BaseCommitSha=(& git -c safe.directory=C:/ueproject/test rev-parse HEAD);SourceFingerprint=(& (Join-Path $PSScriptRoot "GetV9SourceFingerprint.ps1"));Engine='UE 5.8';Target='Editor Development dedicated process + 2 remote clients';Started=$taskStarted.ToString('o');Finished=(Get-Date).ToString('o');SavePrefix=$taskPrefix;Passed=@('AUD8-20 loot competition and carry exclusion','AUD8-21 private server contract and independent quests','Remote AI ownership commands','AUD8-22 late replicated fire ice bridge power','AUD8-23 voluntary disconnect/rejoin and server restart','V9 remote duplicate/stale inventory commands and durable receipts');NotRun=@('Listen mode','Loss/latency injection','Four players','Stress/Cook/package','Visual physics smoothing measurement');Logs=@($taskLogs)}
 $taskResult | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRoot 'Saved/Automation/V807-Result.json') -Encoding utf8
 Write-Output "V807 dedicated two-client closure PASS; logs $taskPrefix"
} finally {
 foreach($taskProc in $taskProcesses){if(!$taskProc.HasExited){Stop-Process -Id $taskProc.Id}}
}
