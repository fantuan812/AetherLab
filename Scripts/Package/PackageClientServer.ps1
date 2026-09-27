param(
 [Parameter(Mandatory=$true)][string]$EngineRoot,
 [string]$ServerEngineRoot='',
 [ValidateSet('Win64','Linux')][string[]]$ServerPlatforms=@('Win64','Linux'),
 [ValidateSet('Client','Server','Both')][string]$Targets='Both',
 [string]$Output='',
 [switch]$SkipBuildEditor
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskProject=Join-Path $taskRoot 'AetherLab.uproject'
$taskCommit=(& git -c "safe.directory=$taskRoot" -C $taskRoot rev-parse HEAD)
if($LASTEXITCODE -ne 0){throw 'Cannot identify source candidate.'}
$taskDirty=@(& git -c "safe.directory=$taskRoot" -C $taskRoot status --porcelain)
if($LASTEXITCODE -ne 0 -or $taskDirty.Count){throw 'Packaging requires a clean, committed candidate; preserve local changes before packaging.'}
if(!$ServerEngineRoot){$ServerEngineRoot=$EngineRoot}
if($Targets -ne 'Client' -and (Test-Path -LiteralPath (Join-Path $ServerEngineRoot 'Engine/Build/InstalledBuild.txt'))){
 throw 'blocked_engine: configure a UE 5.8 source engine capable of Server targets; Editor -server is not a packaged server.'
}
if(!$Output){$Output=Join-Path $taskRoot ('Saved/Release/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))}
$taskOutput=[IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($taskOutput)|Out-Null
# 专服必须由支持 Server target 的引擎构建；失败不得改成编辑器 -server 冒充 Shipping。
$taskBuildTargets=@()
if($Targets -ne 'Server'){$taskBuildTargets+=@{Target='Client';Platform='Win64';Engine=$EngineRoot}}
if($Targets -ne 'Client'){foreach($taskPlatform in $ServerPlatforms){$taskBuildTargets+=@{Target='Server';Platform=$taskPlatform;Engine=$ServerEngineRoot}}}
foreach($taskJob in $taskBuildTargets){
 $taskTarget=$taskJob.Target
 $taskPlatform=$taskJob.Platform
 $taskUat=Join-Path $taskJob.Engine 'Engine/Build/BatchFiles/RunUAT.bat'
 if(!(Test-Path -LiteralPath $taskUat)){throw 'Configured engine toolchain is unavailable.'}
 $taskVersion=Get-Content -LiteralPath (Join-Path $taskJob.Engine 'Engine/Build/Build.version') -Raw|ConvertFrom-Json
 if($taskVersion.MajorVersion -ne 5 -or $taskVersion.MinorVersion -ne 8){throw 'Engine must remain on the locked UE 5.8 release.'}
 $taskDestination=Join-Path $taskOutput ($taskTarget+'-'+$taskPlatform)
 # 使用引擎支持的文件式 Cook，再制作 IoStore；避免 Stage 依赖本机 Zen HTTP 服务。
 $taskArguments=@('BuildCookRun',"-project=$taskProject",'-noP4','-utf8output','-unattended','-ubtargs=-NoUBA -MaxParallelActions=2','-build','-cook','-AdditionalCookerOptions=-SkipZenStore','-stage','-pak','-iostore','-archive',"-archivedirectory=$taskDestination",'-map=/Game/AetherCore/Maps/L_Frontier')
 if($SkipBuildEditor){$taskArguments+='-skipbuildeditor'}
 if($taskTarget -eq 'Server'){$taskArguments+=@('-server','-noclient',"-serverplatform=$taskPlatform",'-serverconfig=Shipping','-servertarget=AetherLabServer')}
 else{$taskArguments+=@('-platform=Win64','-clientconfig=Shipping','-target=AetherLab','-prereqs')}
 & $taskUat @taskArguments
 if($LASTEXITCODE -ne 0){throw "Shipping $taskTarget build failed; no acceptance claimed."}
 $taskFiles=@(Get-ChildItem -LiteralPath $taskDestination -Recurse -File)
 $taskExecutables=@($taskFiles|Where-Object {if($taskPlatform -eq 'Linux'){$_.Name -like 'AetherLabServer*' -and !$_.Extension}else{$_.Extension -eq '.exe'}})
 if(!$taskExecutables){throw "No executable in staged $taskTarget package."}
 if($taskTarget -eq 'Server'){
  $taskForbidden=@(Get-ChildItem -LiteralPath $taskDestination -Recurse -File | Where-Object {$_.Name -eq 'motionbricks.dll' -or $_.Extension -eq '.gguf' -or $_.Extension -eq '.mbstyle'})
  if($taskForbidden){throw 'Dedicated server package unexpectedly contains model/native inference payload.'}
 }
 $taskPayload=@($taskFiles|Where-Object {$_.Extension -ne '.log' -and $_.Name -ne 'build-manifest.json'}|ForEach-Object {
  @{path=$_.FullName.Substring($taskDestination.Length).TrimStart('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
 })
 [ordered]@{schema=2;target=$taskTarget;platform=$taskPlatform;sourceCommit=$taskCommit;dirty=$false;engine=$taskVersion;createdUtc=[DateTime]::UtcNow.ToString('o');configuration='Shipping';buildCookStage='completed';runtimeAcceptance='not_run';releaseAccepted=$false;payload=$taskPayload}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $taskDestination 'build-manifest.json') -Encoding utf8
}
Write-Output "Shipping packages built at $taskOutput; packaged runtime and hardware acceptance still required."
