param([ValidateSet('CPU','Vulkan')][string]$Backend='CPU',[string]$SourceProject='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(!$SourceProject){$SourceProject=$taskRoot}
$taskLock=Get-Content -LiteralPath (Join-Path $taskRoot 'Build/ThirdParty/MotionBricks.lock.json') -Raw | ConvertFrom-Json
$taskBundle=Get-Content -LiteralPath (Join-Path $taskRoot 'Build/ThirdParty/MotionBricks.bundle.json') -Raw | ConvertFrom-Json
$taskBuild=Join-Path $SourceProject "Saved/ThirdParty/build-motion-$Backend"
$taskArtifacts=Get-Content -LiteralPath (Join-Path $taskBuild 'artifacts.json') -Raw | ConvertFrom-Json
if($taskArtifacts.revision -ne $taskLock.revision -or $taskArtifacts.ggml -ne $taskLock.ggmlRevision -or $taskArtifacts.backend -ne $Backend){throw 'Native artifacts differ from the pinned recipe.'}
$taskCache=Get-Content -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt') -Raw
foreach($taskOption in @('MOTIONBRICKS_ENABLE_GGML:BOOL=ON','MOTIONBRICKS_ENABLE_PHYSICS:BOOL=OFF','MOTIONBRICKS_DOWNLOAD_MODELS:BOOL=OFF')){
 if(!$taskCache.Contains($taskOption)){throw "Native artifact option missing: $taskOption"}
}
$taskDestination=[IO.Path]::GetFullPath((Join-Path $taskRoot 'Plugins/AetherMotion/Binaries/ThirdParty/Win64'))
$taskAllowed=[IO.Path]::GetFullPath((Join-Path $taskRoot 'Plugins/AetherMotion/Binaries/ThirdParty'))
if(!$taskDestination.StartsWith($taskAllowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Stage destination escaped plugin build directory.'}
[IO.Directory]::CreateDirectory($taskDestination)|Out-Null
$taskEntries=[System.Collections.Generic.List[object]]::new()
function Copy-VerifiedMotionFile([string]$Source,[string]$Relative,[long]$Bytes,[string]$Sha){
 if($Relative -notmatch '^[A-Za-z0-9_./-]+$' -or $Relative.Contains('..') -or [IO.Path]::IsPathRooted($Relative)){throw 'Invalid stage-relative path.'}
 $taskInput=Get-Item -LiteralPath $Source
 if($taskInput.Length -ne $Bytes -or (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Sha){throw "Artifact hash mismatch: $Relative"}
 $taskTarget=[IO.Path]::GetFullPath((Join-Path $taskDestination $Relative))
 if(!$taskTarget.StartsWith($taskDestination+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'File escaped stage directory.'}
 [IO.Directory]::CreateDirectory((Split-Path -Parent $taskTarget))|Out-Null
 Copy-Item -LiteralPath $Source -Destination $taskTarget -Force
 $taskEntries.Add([ordered]@{path=$Relative;bytes=$Bytes;sha256=$Sha})
}
foreach($taskFile in $taskArtifacts.files){
 Copy-VerifiedMotionFile (Join-Path $taskBuild ('bin/Release/'+$taskFile.name)) $taskFile.name $taskFile.bytes $taskFile.sha256
}
foreach($taskFile in $taskBundle.files){
 Copy-VerifiedMotionFile (Join-Path $SourceProject ('Saved/ThirdParty/MotionBundle/'+$taskFile.path)) $taskFile.path $taskFile.bytes $taskFile.sha256
}
# 自制风格只接受导出来源和 mbstyle 摘要一致的制品；质量报告独立记录。
$taskStyleRoot=Join-Path $taskRoot 'ContentSource/Motion/Styles'
if(Test-Path -LiteralPath $taskStyleRoot){
 foreach($taskStyle in Get-ChildItem -LiteralPath $taskStyleRoot -File -Filter '*.mbstyle'){
  if($taskStyle.BaseName -notmatch '^[A-Za-z0-9_]+$'){throw 'Invalid authored style name.'}
  $taskSourceRecord=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskStyle.FullName,'.source.json')) -Raw | ConvertFrom-Json
  $taskHash=(Get-FileHash -LiteralPath $taskStyle.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  if($taskSourceRecord.styleSha256 -ne $taskHash -or !$taskSourceRecord.sourceFileSha256 -or !$taskSourceRecord.sourceAsset){throw 'Authored style lacks matching source provenance.'}
  Copy-VerifiedMotionFile $taskStyle.FullName ('styles/'+$taskStyle.Name) $taskStyle.Length $taskHash
 }
}
$taskLicenses=@{
 'MotionBricks-LICENSE.txt'='Plugins/AetherMotion/ThirdPartyNotices/MotionBricks-LICENSE.txt'
 'GGML-LICENSE.txt'='Plugins/AetherMotion/ThirdPartyNotices/GGML-LICENSE.txt'
 'Model-LICENSE.txt'='Plugins/AetherMotion/ThirdPartyNotices/Model-LICENSE.txt'
}
foreach($taskPair in $taskLicenses.GetEnumerator()){
 $taskLicense=Join-Path $taskRoot $taskPair.Value
 Copy-VerifiedMotionFile $taskLicense ('licenses/'+$taskPair.Key) (Get-Item -LiteralPath $taskLicense).Length (Get-FileHash -LiteralPath $taskLicense -Algorithm SHA256).Hash.ToLowerInvariant()
}
# 检查 DLL 清单，拒绝把前一次不同配方留下的未声明后端混入运行时搜索。
$taskKnown=@($taskEntries|ForEach-Object {$_.path})
foreach($taskDll in Get-ChildItem -LiteralPath $taskDestination -File -Filter '*.dll'){
 if($taskKnown -notcontains $taskDll.Name){throw "Unlisted runtime DLL exists; inspect the stage directory: $($taskDll.Name)"}
}
$taskManifest=[ordered]@{schemaVersion=1;nativeRevision=$taskLock.revision;ggmlRevision=$taskLock.ggmlRevision;abi=$taskLock.abi;backend=$Backend;modelRevision=$taskLock.modelRevision;files=@($taskEntries)}
$taskTemporary=Join-Path $taskDestination 'stage.json.tmp'
[IO.File]::WriteAllText($taskTemporary,($taskManifest|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $taskTemporary -Destination (Join-Path $taskDestination 'stage.json') -Force
Write-Output "Motion NonUFS stage prepared: $taskDestination. This is not an inference or performance acceptance result."
