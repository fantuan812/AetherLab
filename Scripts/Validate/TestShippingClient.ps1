param(
 [Parameter(Mandatory=$true)][string]$PackageRoot,
 [ValidateSet('Traditional','CPU','Vulkan')][string]$Backend='Traditional'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskSourceCommit=& git -C $taskRoot rev-parse HEAD
$taskSourceChanges=@(& git -C $taskRoot status --porcelain -- Source Plugins Scripts Content ContentSource Build)
$taskPackage=[IO.Path]::GetFullPath($PackageRoot)
$taskExe=Join-Path $taskPackage 'AetherLab/Binaries/Win64/AetherLab-Win64-Shipping.exe'
if(!(Test-Path -LiteralPath $taskExe)){throw 'Explicit archived Windows package required'}
$taskStage=Join-Path $taskPackage 'AetherLab/Plugins/AetherMotion/Binaries/ThirdParty/Win64'
$taskManifest=Get-Content -LiteralPath (Join-Path $taskStage 'stage.json') -Raw|ConvertFrom-Json
foreach($taskFile in $taskManifest.files){
 $taskPath=[IO.Path]::GetFullPath((Join-Path $taskStage $taskFile.path))
 if(!$taskPath.StartsWith($taskStage+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Stage manifest escaped runtime directory'}
 if(!(Test-Path -LiteralPath $taskPath) -or (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskFile.sha256){throw "Packaged runtime hash mismatch: $($taskFile.path)"}
}
$taskToken=[guid]::NewGuid().ToString('N')
$taskDir=Join-Path $taskRoot ("Saved/Automation/V10Package_"+$taskToken)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskUser=Join-Path $taskDir 'User'
$taskExpected=Join-Path $taskUser ("Saved/PackageCapture/"+$taskToken)
$taskBackend=@{Traditional=0;CPU=1;Vulkan=2}[$Backend]
$taskArgs=@('/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode',"-AetherPackageCapture=$taskToken",("-AetherSavePrefix=V10Package_"+$taskToken.Substring(0,12)),"-AetherPackageBackend=$taskBackend",('-UserDir="'+$taskUser+'"'),'-RenderOffscreen','-ForceRes','-windowed','-ResX=1920','-ResY=1080','-unattended','-nosound','-NoVSync')
$taskP=Start-Process -FilePath $taskExe -WorkingDirectory $taskPackage -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
$taskModules=[System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
try{
 $taskDeadline=(Get-Date).AddSeconds(180)
 while(!$taskP.HasExited){
  if((Get-Date) -gt $taskDeadline){throw "Shipping capture deadline: $taskDir"}
  try{$taskP.Refresh();foreach($taskModule in $taskP.Modules){if($taskModule.ModuleName -match 'motionbricks|ggml'){[void]$taskModules.Add($taskModule.FileName)}}}catch{}
  Start-Sleep -Milliseconds 500
 }
 $taskP.WaitForExit()
 $taskResult=Get-Content -LiteralPath (Join-Path $taskExpected 'result.json') -Raw|ConvertFrom-Json
 if($taskP.ExitCode -ne 0 -or !$taskResult.passed -or !$taskResult.shipping){throw "Shipping capture failed: $taskDir"}
 foreach($taskName in @('World','Inventory','Journal','Skills','Map','Party','Settings')){
  $taskImage=Join-Path $taskExpected ($taskName+'.png')
  if(!(Test-Path -LiteralPath $taskImage)){throw "Packaged screenshot missing: $taskName"}
 }
 if($Backend -ne 'Traditional'){
  if($taskResult.nativeCalls -lt 2 -or !$taskModules){throw 'Packaged native inference absent'}
  foreach($taskModule in $taskModules){if(!$taskModule.StartsWith($taskPackage+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw "Native DLL loaded from outside package: $taskModule"}}
 }
 [ordered]@{schema=1;sourceCommit=$taskSourceCommit;sourceWorkingTreeChanges=$taskSourceChanges;backend=$Backend;package=$taskPackage;executableSha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant();result=$taskResult;nativeModules=@($taskModules);captures=$taskExpected;scope='Shipping boot and actual menu captures; not full mainline or clean-machine/performance acceptance'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskDir 'package-result.json') -Encoding utf8
 Write-Output "Shipping capture PASS ($Backend): $taskDir"
}finally{if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}
