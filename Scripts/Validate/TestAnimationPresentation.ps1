param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [ValidateSet('Traditional','CPU','Vulkan')][string]$Backend='CPU'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskToken=[guid]::NewGuid().ToString('N')
$taskDir=Join-Path $taskRoot ('Saved/Automation/V10Pose_'+$taskToken)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskBackend=@{Traditional=0;CPU=1;Vulkan=2}[$Backend]
$taskArgs=@(
 ('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),
 '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode','-game',
 ('-AetherPackageCapture='+$taskToken),("-AetherPackageBackend=$taskBackend"),
 ('-AetherSavePrefix=V10Package_'+$taskToken.Substring(0,12)),('-UserDir="'+$taskDir+'/User"'),
 '-RenderOffscreen','-ForceRes','-windowed','-ResX=1280','-ResY=720','-unattended','-nosound',
 '-ExecCmds="t.MaxFPS 60"',('-abslog="'+$taskDir+'/Engine.log"')
)
$taskP=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try{
 if(!$taskP.WaitForExit(180000)){throw "Presentation deadline: $taskDir"}
 $taskResult=Get-Content -LiteralPath (Join-Path $taskDir ("User/Saved/PackageCapture/$taskToken/result.json")) -Raw|ConvertFrom-Json
 if($taskP.ExitCode -ne 0 -or !$taskResult.passed -or $taskResult.bodyPosedBones -lt 8){throw "Actual body graph failed: $taskDir"}
 if($taskBackend -gt 0 -and ($taskResult.sourcePosedBones -lt 8 -or $taskResult.generatedWeight -lt .9)){throw "Generated graph failed: $taskDir"}
 Write-Output "Actual animation graph PASS ($Backend): $taskDir. Visual quality is reviewed separately."
}finally{if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}
