param(
 [Parameter(Mandatory=$true)][string]$EngineRoot,
 [ValidateSet('Client','Server','Both')][string]$Targets='Both',
 [string]$Output=''
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskProject=Join-Path $taskRoot 'AetherLab.uproject'
if(!$Output){$Output=Join-Path $taskRoot ('Saved/Release/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))}
$taskOutput=[IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($taskOutput)|Out-Null
# 专服必须由支持 Server target 的引擎构建；失败不得改成编辑器 -server 冒充 Shipping。
$taskUat=Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
$taskBuildTargets=if($Targets -eq 'Both'){@('Client','Server')}else{@($Targets)}
foreach($taskTarget in $taskBuildTargets){
 $taskDestination=Join-Path $taskOutput $taskTarget
 $taskArguments=@('BuildCookRun',"-project=$taskProject",'-noP4','-utf8output','-unattended','-build','-cook','-stage','-pak','-iostore','-archive',"-archivedirectory=$taskDestination",'-map=/Game/AetherCore/Maps/L_Frontier')
 if($taskTarget -eq 'Server'){$taskArguments+=@('-server','-noclient','-serverplatform=Win64','-serverconfig=Shipping','-servertarget=AetherLabServer')}
 else{$taskArguments+=@('-platform=Win64','-clientconfig=Shipping','-target=AetherLab','-prereqs')}
 & $taskUat @taskArguments
 if($LASTEXITCODE -ne 0){throw "Shipping $taskTarget build failed; no acceptance claimed."}
 $taskExecutables=@(Get-ChildItem -LiteralPath $taskDestination -Recurse -File -Filter '*.exe')
 if(!$taskExecutables){throw "No executable in staged $taskTarget package."}
 if($taskTarget -eq 'Server'){
  $taskForbidden=@(Get-ChildItem -LiteralPath $taskDestination -Recurse -File | Where-Object {$_.Name -eq 'motionbricks.dll' -or $_.Extension -eq '.gguf' -or $_.Extension -eq '.mbstyle'})
  if($taskForbidden){throw 'Dedicated server package unexpectedly contains model/native inference payload.'}
 }
 [ordered]@{schema=1;target=$taskTarget;sourceCommit=(& git -C $taskRoot rev-parse HEAD);createdUtc=[DateTime]::UtcNow.ToString('o');configuration='Shipping';runtimeAcceptance='pending';executables=@($taskExecutables|ForEach-Object {@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $taskDestination 'build-manifest.json') -Encoding utf8
}
Write-Output "Shipping packages built at $taskOutput; packaged runtime and hardware acceptance still required."
