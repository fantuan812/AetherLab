param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskPrefix='AetherV9_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
foreach($taskPhase in 'Run','Reload') {
 $taskLog=Join-Path $taskRoot "Saved\Logs\$taskPrefix-$taskPhase.log"
 $taskArgs=@('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"','/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile=V9Check','-game','-nullrhi','-nosound','-unattended','-nop4','-nosplash','-AetherV9Check',"-AetherV9Phase=$taskPhase","-AetherSavePrefix=$taskPrefix","-abslog=$taskLog")
 $taskProcess=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
 try {
  if(!$taskProcess.WaitForExit(60000)){throw "V9 $taskPhase timed out: $taskLog"}
  if($taskProcess.ExitCode -ne 0 -or !(Select-String -LiteralPath $taskLog -Pattern "AETHER_V9_PASS phase=$taskPhase failures=0" -Quiet)){throw "V9 $taskPhase failed: $taskLog"}
 } finally {if(!$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
 Write-Output "V9 $taskPhase passed: $taskLog"
}
