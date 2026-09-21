param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [ValidateSet('CPU','Vulkan')][string]$Backend='CPU'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskToken='V10Quality_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskDir=Join-Path $taskRoot ('Saved/Automation/'+$taskToken)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskBackend=if($Backend -eq 'CPU'){1}else{2}
$taskArgs=@(
 ('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),
 '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode','-game','-AetherMotionQuality',
 ("-AetherQualityBackend=$taskBackend"),("-AetherSavePrefix=$taskToken"),('-AetherQualityReport="'+$taskDir+'"'),
 '-RenderOffscreen','-ForceRes','-windowed','-ResX=1280','-ResY=720','-unattended','-nosound',
 '-ExecCmds="t.MaxFPS 60"',('-abslog="'+$taskDir+'/Engine.log"')
)
$taskP=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try{
 $taskDeadline=(Get-Date).AddSeconds(1100)
 while(!$taskP.HasExited){if((Get-Date) -gt $taskDeadline){throw "Quality deadline: $taskDir"};Start-Sleep -Milliseconds 500}
 $taskP.WaitForExit()
 if($taskP.ExitCode -ne 0 -or !(Test-Path -LiteralPath (Join-Path $taskDir 'result.json'))){throw "Quality process failed: $taskDir/Engine.log"}
 $taskResult=Get-Content -LiteralPath (Join-Path $taskDir 'result.json') -Raw|ConvertFrom-Json
 if($taskP.ExitCode -ne 0 -or !$taskResult.graphChecksPassed -or $taskResult.styles.Count -ne 16){throw "Quality graph failed: $taskDir"}
 if(@(Get-ChildItem -LiteralPath $taskDir -Filter '*.png').Count -ne 32){throw "Incomplete actual screenshots: $taskDir"}
 Write-Output "Actual 16 body/style graph checks PASS ($Backend): $taskDir. Visual/contact/performance review remains separate."
}finally{if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}
