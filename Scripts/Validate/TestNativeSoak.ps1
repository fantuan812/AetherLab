param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [ValidateRange(600,7200)][int]$Seconds=3600
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskSourceCommit=& git -C $taskRoot rev-parse HEAD
$taskSourceChanges=@(& git -C $taskRoot status --porcelain -- Source Plugins Scripts Content ContentSource Build)
$taskPrefix='V10Soak_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskDir=Join-Path $taskRoot ("Saved/Automation/"+$taskPrefix)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskLog=Join-Path $taskDir 'Engine.log'
$taskReport=Join-Path $taskDir 'result.json'
$taskArgs=@(
 ('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),
 '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode',
 '-game','-AetherV10Soak',"-AetherSavePrefix=$taskPrefix","-AetherSoakSeconds=$Seconds",
 ('-AetherSoakReport="'+$taskReport+'"'),'-RenderOffscreen','-ForceRes','-windowed','-ResX=1920','-ResY=1080',
 '-unattended','-nosound','-nop4','-NoVSync','-ExecCmds="t.MaxFPS 60,r.ScreenPercentage 100"',
 ('-abslog="'+$taskLog+'"')
)
$taskP=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try{
 $taskDeadline=(Get-Date).AddSeconds($Seconds+180)
 while(!$taskP.HasExited){
  if((Get-Date) -gt $taskDeadline){throw "Soak deadline: $taskLog"}
  Start-Sleep -Milliseconds 500
 }
 $taskP.WaitForExit()
 if($taskP.ExitCode -ne 0 -or !(Test-Path -LiteralPath $taskReport)){throw "Soak failed: $taskLog"}
 if(Select-String -LiteralPath $taskLog -Pattern 'V10_SOAK_FAIL|Fatal error:' -Quiet){throw "Soak failure marker: $taskLog"}
 $taskResult=Get-Content -LiteralPath $taskReport -Raw|ConvertFrom-Json
 if($taskResult.seconds -lt $Seconds -or $taskResult.replacements -lt 50 -or $taskResult.regionTravels -lt 50 -or $taskResult.menuCycles -lt 100){throw 'Lifecycle coverage incomplete'}
 [ordered]@{schema=1;sourceCommit=$taskSourceCommit;sourceWorkingTreeChanges=$taskSourceChanges;requestedSeconds=$Seconds;report=$taskReport;sourceScope='Editor Development; not full mainline or Shipping'}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $taskDir 'run-manifest.json') -Encoding utf8
 Write-Output "Native lifecycle run completed: $taskReport. Frame/memory budgets and visual quality require review; this is not Shipping/full-mainline acceptance."
}finally{if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}
