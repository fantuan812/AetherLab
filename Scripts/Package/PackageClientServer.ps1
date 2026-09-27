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
if(!$Output){$Output=Join-Path $taskRoot ('Saved/Release/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))}
$taskOutput=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath (Join-Path $taskOutput 'candidate-index.json')){throw 'Candidate output is already recorded; choose a fresh output directory.'}
[IO.Directory]::CreateDirectory($taskOutput)|Out-Null
# 专服必须由支持 Server target 的引擎构建；失败不得改成编辑器 -server 冒充 Shipping。
$taskBuildTargets=@()
if($Targets -ne 'Server'){$taskBuildTargets+=@{Target='Client';Platform='Win64';Engine=$EngineRoot}}
if($Targets -ne 'Client'){foreach($taskPlatform in $ServerPlatforms){$taskBuildTargets+=@{Target='Server';Platform=$taskPlatform;Engine=$ServerEngineRoot}}}
$taskMatrix=[ordered]@{schema=1;candidateCommit=$taskCommit;releaseAccepted=$false;targets=[ordered]@{
 'Client-Win64'=@{status='not_run';runtimeAcceptance='not_run'};
 'Server-Win64'=@{status='not_run';runtimeAcceptance='not_run'};
 'Server-Linux'=@{status='not_run';runtimeAcceptance='not_run'}}}
$taskFailures=@()
foreach($taskJob in $taskBuildTargets){
 $taskTarget=$taskJob.Target
 $taskPlatform=$taskJob.Platform
 $taskKey=$taskTarget+'-'+$taskPlatform
 $taskCapability=[ordered]@{status='not_run';runtimeAcceptance='not_run';serverSupport='pending_ubt';sdk='not_checked';engine=$taskJob.Engine;engineKind= $(if(Test-Path -LiteralPath (Join-Path $taskJob.Engine 'Engine/Build/InstalledBuild.txt')){'installed'}else{'source'});reason=''}
 $taskMatrix.targets[$taskKey]=$taskCapability
 try {
 $taskUat=Join-Path $taskJob.Engine 'Engine/Build/BatchFiles/RunUAT.bat'
 if(!(Test-Path -LiteralPath $taskUat)){throw 'Configured engine toolchain is unavailable.'}
 $taskVersion=Get-Content -LiteralPath (Join-Path $taskJob.Engine 'Engine/Build/Build.version') -Raw|ConvertFrom-Json
 if($taskVersion.MajorVersion -ne 5 -or $taskVersion.MinorVersion -ne 8){throw 'Engine must remain on the locked UE 5.8 release.'}
 $taskDestination=Join-Path $taskOutput ($taskTarget+'-'+$taskPlatform)
 if(Test-Path -LiteralPath $taskDestination){throw "Candidate destination already exists; use a fresh output directory: $taskDestination"}
 [IO.Directory]::CreateDirectory($taskDestination)|Out-Null
 $taskCapability.engineBuild=$taskVersion
 $taskUbt=Join-Path $taskJob.Engine 'Engine/Build/BatchFiles/RunUBT.bat'
 if(!(Test-Path -LiteralPath $taskUbt)){throw 'blocked_engine: UBT entry point unavailable.'}
 $taskSdkLog=Join-Path $taskDestination 'sdk.log'
 $taskSdkOutput=@(& $taskUbt '-Mode=ValidatePlatforms' "-Platforms=$taskPlatform" '-OutputSDKs' 2>&1)
 $taskSdkExit=$LASTEXITCODE
 $taskSdkOutput|Set-Content -LiteralPath $taskSdkLog -Encoding utf8
 if($taskSdkExit -ne 0 -or ($taskSdkOutput -join "`n") -notmatch ("##PlatformValidate:\s+"+[regex]::Escape($taskPlatform)+"\s+VALID(?:\s|$)")){
  $taskCapability.sdk='blocked';throw "blocked_sdk: required $taskPlatform SDK is not confirmed; see $taskSdkLog"
 }
 $taskCapability.sdk='valid'
 $taskJobStartedUtc=[DateTime]::UtcNow
 # 使用引擎支持的文件式 Cook，再制作 IoStore；避免 Stage 依赖本机 Zen HTTP 服务。
 $taskArguments=@('BuildCookRun',"-project=$taskProject",'-noP4','-utf8output','-unattended','-ubtargs=-NoUBA -MaxParallelActions=2','-build','-cook','-AdditionalCookerOptions=-SkipZenStore','-stage','-pak','-iostore','-archive',"-archivedirectory=$taskDestination",'-map=/Game/AetherCore/Maps/L_Frontier')
 if($SkipBuildEditor){$taskArguments+='-skipbuildeditor'}
 if($taskTarget -eq 'Server'){$taskArguments+=@('-server','-noclient',"-serverplatform=$taskPlatform",'-serverconfig=Shipping','-servertarget=AetherLabServer')}
 else{$taskArguments+=@('-platform=Win64','-clientconfig=Shipping','-target=AetherLab','-prereqs')}
 $taskUatLog=Join-Path $taskDestination 'uat.log'
 & $taskUat @taskArguments 2>&1|Tee-Object -FilePath $taskUatLog
 if($LASTEXITCODE -ne 0){
  if($taskTarget -eq 'Server' -and (Get-Content -LiteralPath $taskUatLog -Raw) -match '(?i)(server targets? (are |is )?not (currently )?supported|not.*valid platform.*configuration|missing precompiled manifest)'){
   $taskCapability.serverSupport='blocked';throw "blocked_engine: server target rejected by UBT; see $taskUatLog"
  }
  throw "Shipping $taskTarget build failed; see $taskUatLog; no acceptance claimed."
 }
 $taskCapability.serverSupport=if($taskTarget -eq 'Server'){'confirmed_by_ubt'}else{'not_applicable'}
 $taskFiles=@(Get-ChildItem -LiteralPath $taskDestination -Recurse -File)
 $taskStageFolder=if($taskTarget -eq 'Client'){'Windows'}elseif($taskPlatform -eq 'Win64'){'WindowsServer'}else{'LinuxServer'}
 $taskExecutableRelative=if($taskTarget -eq 'Client'){'Windows/AetherLab.exe'}elseif($taskPlatform -eq 'Win64'){'WindowsServer/AetherLabServer.exe'}else{'LinuxServer/AetherLab/Binaries/Linux/AetherLabServer-Linux-Shipping'}
 $taskExecutable=Join-Path $taskDestination $taskExecutableRelative
 if(!(Test-Path -LiteralPath $taskExecutable -PathType Leaf)){throw "Expected target executable missing: $taskExecutableRelative"}
 if($taskPlatform -eq 'Linux'){
  if($taskFiles|Where-Object {$_.Extension -eq '.dll'}){throw 'Linux server contains Windows dynamic libraries.'}
  $taskLinuxBinaries=@(Get-Item -LiteralPath $taskExecutable)+@($taskFiles|Where-Object {$_.Name -match '\.so(?:\.\d+)*$'})
  foreach($taskBinary in $taskLinuxBinaries){
   $taskStream=[IO.File]::OpenRead($taskBinary.FullName)
   try{$taskMagic=New-Object byte[] 20; $taskRead=$taskStream.Read($taskMagic,0,20)}finally{$taskStream.Dispose()}
   if($taskRead -ne 20 -or $taskMagic[0] -ne 127 -or $taskMagic[1] -ne 69 -or $taskMagic[2] -ne 76 -or $taskMagic[3] -ne 70 -or $taskMagic[4] -ne 2 -or $taskMagic[5] -ne 1 -or $taskMagic[18] -ne 62 -or $taskMagic[19] -ne 0){throw "Linux main executable and libraries must be x86-64 ELF: $($taskBinary.Name)"}
  }
 }
 $taskInferencePattern='(?i)(motionbricks|ggml|\.gguf(?:\W|$)|\.mbstyle(?:\W|$)|g1-f32|AetherMotion[/\\]Binaries[/\\]ThirdParty)'
 $taskEvidence=Join-Path $taskDestination 'build-evidence'
 [IO.Directory]::CreateDirectory($taskEvidence)|Out-Null
 $taskStageRoot=Join-Path $taskRoot ('Saved/StagedBuilds/'+$taskStageFolder)
 $taskStageManifests=@(Get-ChildItem -LiteralPath $taskStageRoot -Recurse -File -ErrorAction SilentlyContinue|Where-Object {$_.Name -match '^Manifest_(UFS|NonUFS|Debug)Files_.*\.txt$' -and $_.LastWriteTimeUtc -ge $taskJobStartedUtc.AddMinutes(-1)})
 if(!$taskStageManifests){throw "UAT stage manifests missing for $taskTarget/$taskPlatform; content cannot be audited."}
 $taskStageEvidence=@()
 foreach($taskManifest in $taskStageManifests){
  $taskText=Get-Content -LiteralPath $taskManifest -Raw
  if($taskTarget -eq 'Server' -and $taskText -match $taskInferencePattern){throw "Server stage manifest contains inference payload: $($taskManifest.Name)"}
  $taskCopy=Join-Path $taskEvidence ($taskPlatform+'-'+$taskManifest.Name)
  Copy-Item -LiteralPath $taskManifest.FullName -Destination $taskCopy -Force
  $taskStageEvidence+=@{name=$taskManifest.Name;sha256=(Get-FileHash -LiteralPath $taskCopy -Algorithm SHA256).Hash.ToLowerInvariant()}
 }
 $taskUnrealPak=Join-Path $taskJob.Engine 'Engine/Binaries/Win64/UnrealPak.exe'
 $taskContainers=@($taskFiles|Where-Object {$_.Extension -in @('.pak','.utoc')})
 if(!$taskContainers){throw "No Pak/IoStore content containers found in $taskTarget/$taskPlatform."}
 if(!(Test-Path -LiteralPath $taskUnrealPak -PathType Leaf)){throw 'UnrealPak listing tool unavailable; content containers cannot be audited.'}
 $taskContainerEvidence=@()
 foreach($taskContainer in $taskContainers){
  $taskListing=@(& $taskUnrealPak $taskContainer.FullName '-List' 2>&1)
  if($LASTEXITCODE -ne 0){throw "Container listing failed: $($taskContainer.Name)"}
  $taskListingText=$taskListing -join [Environment]::NewLine
  if([string]::IsNullOrWhiteSpace($taskListingText) -or $taskListingText -match '(?im)(Error:|Failed to|Unable to open)'){throw "Container listing did not produce trustworthy content: $($taskContainer.Name)"}
  if($taskTarget -eq 'Server' -and $taskListingText -match $taskInferencePattern){throw "Server content container contains inference payload: $($taskContainer.Name)"}
  $taskListingPath=Join-Path $taskEvidence ($taskContainer.Name+'.listing.txt')
  [IO.File]::WriteAllText($taskListingPath,$taskListingText)
  $taskContainerEvidence+=@{path=$taskContainer.FullName.Substring($taskDestination.Length).TrimStart('\','/');sha256=(Get-FileHash -LiteralPath $taskContainer.FullName -Algorithm SHA256).Hash.ToLowerInvariant();listingSha256=(Get-FileHash -LiteralPath $taskListingPath -Algorithm SHA256).Hash.ToLowerInvariant()}
 }
 if($taskTarget -eq 'Server'){
  $taskForbidden=@($taskFiles|Where-Object {$_.FullName -match $taskInferencePattern})
  if($taskForbidden){throw 'Dedicated server package unexpectedly contains model/native inference payload.'}
 }
 $taskPayload=@($taskFiles|Where-Object {$_.Extension -ne '.log' -and $_.Name -ne 'build-manifest.json'}|ForEach-Object {
  @{path=$_.FullName.Substring($taskDestination.Length).TrimStart('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
 })
 $taskAfterCommit=(& git -c "safe.directory=$taskRoot" -C $taskRoot rev-parse HEAD)
 $taskAfterDirty=@(& git -c "safe.directory=$taskRoot" -C $taskRoot status --porcelain)
 if($LASTEXITCODE -ne 0 -or $taskAfterCommit -cne $taskCommit -or $taskAfterDirty.Count){throw 'Candidate source changed during packaging.'}
 $taskCapability.status='passed'
 [ordered]@{schema=4;target=$taskTarget;platform=$taskPlatform;sourceCommit=$taskCommit;dirty=$false;engine=$taskVersion;capability=$taskCapability;createdUtc=[DateTime]::UtcNow.ToString('o');configuration='Shipping';executable=$taskExecutableRelative;buildCookStage='completed';stageManifests=$taskStageEvidence;contentContainers=$taskContainerEvidence;toolchainEvidence='sdk.log / uat.log / engine Build.version';runtimeAcceptance='not_run';licenseReview='not_run';releaseAccepted=$false;payload=$taskPayload}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $taskDestination 'build-manifest.json') -Encoding utf8
 $taskCapability.evidence=Join-Path $taskDestination 'build-manifest.json'
 }catch{
  $taskCapability.reason=$_.Exception.Message
  $taskCapability.status=if($_.Exception.Message -match '^blocked_'){'blocked'}else{'failed'}
  $taskFailures+=$taskKey+': '+$_.Exception.Message
 }finally{
  $taskMatrix|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $taskOutput 'candidate-index.json') -Encoding utf8
 }
}
if($taskFailures.Count){throw ($taskFailures -join [Environment]::NewLine)}
Write-Output "Shipping packages built at $taskOutput; packaged runtime and hardware acceptance still required."
