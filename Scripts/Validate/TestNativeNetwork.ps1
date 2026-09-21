param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [ValidateSet('Dedicated','Listen')][string]$Mode='Dedicated',
 [int]$Port=7853,
 [switch]$Impaired
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskPrefix='V10Native_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskDir=Join-Path $taskRoot ("Saved/Automation/"+$taskPrefix)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskProcesses=[System.Collections.Generic.List[object]]::new()
$taskExe=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskImpair=if($Impaired){@('-PktLag=50','-PktLoss=3')}else{@('-PktLag=0','-PktLoss=0')}
function Start-Native([string]$Name,[string[]]$Extra){
 $taskLog=Join-Path $taskDir ($Name+'.log')
 $taskArgs=@('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"')+$Extra+@('-game','-nullrhi','-nosound','-unattended','-nop4','-NoSplash','-AetherV10NativeNetwork','-ExecCmds="t.MaxFPS 30"',('-abslog="'+$taskLog+'"'))+$taskImpair
 $taskP=Start-Process -FilePath $taskExe -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
 $taskProcesses.Add($taskP)
 return @{Process=$taskP;Log=$taskLog}
}
function Wait-Native($Task,[string]$Marker,[int]$Seconds=110){
 $taskUntil=(Get-Date).AddSeconds($Seconds)
 while($true){
  foreach($taskOther in (Get-ChildItem -LiteralPath $taskDir -Filter "*.log")){if(Select-String -LiteralPath $taskOther.FullName -Pattern "V10_NATIVE_NETWORK_FAIL|Fatal error:|AETHER_NATIVE_SCENE_FAILED" -Quiet){throw "Native runtime failure: $($taskOther.FullName)"}}
  if(Test-Path -LiteralPath $Task.Log){
   if(Select-String -LiteralPath $Task.Log -Pattern 'V10_NATIVE_NETWORK_FAIL|Fatal error:|AETHER_NATIVE_SCENE_FAILED' -Quiet){throw "Native runtime failure: $($Task.Log)"}
   if(Select-String -LiteralPath $Task.Log -SimpleMatch $Marker -Quiet){return}
  }
  if($Task.Process.HasExited -or (Get-Date) -gt $taskUntil){throw "Missing $Marker : $($Task.Log)"}
  Start-Sleep -Milliseconds 250
 }
}
function Stop-Native($Task){if(!$Task.Process.HasExited){Stop-Process -Id $Task.Process.Id;$Task.Process.WaitForExit()}}
function Server-Native([string]$Name,[switch]$Recovery){
 $taskMap='/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode'
 $taskExtra=@("-AetherSavePrefix=$taskPrefix",'-AetherDevelopmentIdentities',"-port=$Port")
 if($Mode -eq 'Dedicated'){$taskExtra+='-server'}else{$taskMap+='?listen'}
 if($Recovery){$taskExtra+=@('-AetherNativeRecovery','-AetherProbePlayers=1')}
 return Start-Native $Name (@($taskMap)+$taskExtra)
}
function Client-Native([string]$Name,[string]$Owner,[switch]$Recovery,[int]$Count=4){
 $taskExtra=@(("127.0.0.1:"+$Port+"?DevProfile="+$Owner),"-AetherProbePlayers=$Count")
 if($Recovery){$taskExtra+='-AetherNativeRecovery'}
 return Start-Native $Name $taskExtra
}
try{
 $taskServer=Server-Native 'Server'
 Wait-Native $taskServer 'AETHER_V10_SCENE_READY'
 $taskOwners=if($Mode -eq 'Dedicated'){@('Alpha','Beta','Gamma','Delta')}else{@('Alpha','Beta','Gamma')}
 $taskClients=@()
 for($taskIndex=0;$taskIndex -lt $taskOwners.Count;$taskIndex++){
  if($taskIndex -eq $taskOwners.Count-1){Start-Sleep -Seconds 5}
  $taskClients+=Client-Native $taskOwners[$taskIndex] $taskOwners[$taskIndex]
 }
 foreach($taskClient in $taskClients){Wait-Native $taskClient 'V10_NATIVE_NETWORK_PASS'}
 if($Mode -eq 'Listen'){Wait-Native $taskServer 'V10_NATIVE_NETWORK_PASS'}
 Write-Output 'Four-player native commands, private snapshots and late join PASS.'
 Stop-Native $taskClients[0]
 Start-Sleep -Seconds 3
 $taskReconnect=Client-Native 'AlphaReconnect' 'Alpha' -Recovery
 Wait-Native $taskReconnect 'V10_NATIVE_NETWORK_PASS'
 Write-Output 'Disconnected player durable inventory recovery PASS.'
 foreach($taskClient in $taskClients){Stop-Native $taskClient}
 Stop-Native $taskReconnect
 # 只终止此脚本创建的合成服务器。重新打开相同数据库验证持久恢复，绝不触碰个人存档。
 Stop-Native $taskServer
 $taskServer=Server-Native 'ServerRestart' -Recovery
 Wait-Native $taskServer 'AETHER_V10_SCENE_READY'
 $taskRestart=Client-Native 'AlphaRestart' 'Alpha' -Recovery -Count 1
 Wait-Native $taskRestart 'V10_NATIVE_NETWORK_PASS'
 Write-Output 'Server restart durable inventory recovery PASS.'
 [ordered]@{schema=1;mode=$Mode;target='Editor Development';players=4;outgoingLagMs=$(if($Impaired){50}else{0});lossPercent=$(if($Impaired){3}else{0});passed=@('native commands','owner privacy','duplicate request','late join','disconnect reconnect','server restart');shipping=$false;sourceCommit=(& git -C $taskRoot rev-parse HEAD);logs=$taskDir}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $taskDir 'result.json') -Encoding utf8
 Write-Output "Native network PASS: $taskDir"
}finally{foreach($taskP in $taskProcesses){if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}}
