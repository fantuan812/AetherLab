param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskPrefix='V10Journey_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskDir=Join-Path $taskRoot ('Saved/Automation/'+$taskPrefix)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskReport=Join-Path $taskDir 'result.json'
$taskArgs=@(
 ('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),
 '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode',
 '-game','-AetherNativeJourney',("-AetherSavePrefix=$taskPrefix"),
 ('-AetherJourneyReport="'+$taskReport+'"'),'-nullrhi','-unattended','-nosound',
 '-ExecCmds="t.MaxFPS 30"',('-abslog="'+$taskDir+'/Engine.log"')
)
$taskP=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try{
 $taskDeadline=(Get-Date).AddSeconds(1200)
 while(!$taskP.HasExited){if((Get-Date) -gt $taskDeadline){throw "Journey deadline: $taskDir"};Start-Sleep -Milliseconds 500}
 $taskP.WaitForExit()
 if($taskP.ExitCode -ne 0 -or !(Test-Path -LiteralPath $taskReport)){throw "Journey failed: $taskDir/Engine.log"}
 $taskResult=Get-Content -LiteralPath $taskReport -Raw|ConvertFrom-Json
 if(!$taskResult.passed){throw "Journey failed: $taskDir"}
 Write-Output "Native journey PASS quests=$($taskResult.questCount) fullMainline=$($taskResult.fullMainline): $taskDir"
}finally{if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}
