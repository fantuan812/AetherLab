param([ValidateSet('CPU','Vulkan')][string]$Backend='CPU',[string]$CMake='cmake')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskLock=Get-Content (Join-Path $taskRoot 'Build/ThirdParty/MotionBricks.lock.json') -Raw | ConvertFrom-Json
$taskSource=Join-Path $taskRoot 'Saved/ThirdParty/motion-bricks'
$taskBuild=Join-Path $taskRoot "Saved/ThirdParty/build-motion-$Backend"
# 构建缓存可以删除；源码和版本锁是重现构建的依据，脚本不跟随上游 main。
if(!(Test-Path -LiteralPath (Join-Path $taskSource '.git'))){
 & git clone --no-checkout $taskLock.repository $taskSource
 if($LASTEXITCODE -ne 0){throw 'Native source clone failed.'}
 & git -c "safe.directory=$taskSource" -C $taskSource checkout --detach $taskLock.revision
 if($LASTEXITCODE -ne 0){throw 'Pinned source checkout failed.'}
}
$taskRevision=& git -c "safe.directory=$taskSource" -C $taskSource rev-parse HEAD
if($taskRevision -ne $taskLock.revision){throw 'Native source revision differs from the lock; refusing an implicit upgrade.'}
& git -c "safe.directory=$taskSource" -C $taskSource submodule update --init --recursive
if($LASTEXITCODE -ne 0){throw 'GGML checkout failed.'}
$taskGgml=Join-Path $taskSource 'ggml'
if((& git -c "safe.directory=$taskGgml" -C $taskGgml rev-parse HEAD) -ne $taskLock.ggmlRevision){throw 'GGML revision mismatch.'}
$taskVulkan=if($Backend -eq 'Vulkan'){'ON'}else{'OFF'}
# 单独使用 C++23 和 DLL 的运行库设置，不改变 UE 游戏模块的 C++ 标准。
& $CMake -S $taskSource -B $taskBuild -G 'Visual Studio 17 2022' -A x64 '-DMOTIONBRICKS_ENABLE_GGML=ON' '-DMOTIONBRICKS_ENABLE_PHYSICS=OFF' '-DMOTIONBRICKS_DOWNLOAD_MODELS=OFF' '-DMOTIONBRICKS_CPU_ALL_VARIANTS=ON' "-DMOTIONBRICKS_ENABLE_VULKAN=$taskVulkan" '-DMOTIONBRICKS_BUILD_TESTS=ON'
if($LASTEXITCODE -ne 0){throw 'Native configure failed.'}
& $CMake --build $taskBuild --config Release --target motionbricks_shared motionbricks-cli --parallel 6
if($LASTEXITCODE -ne 0){throw 'Native build failed.'}
# 仅记录本次真实构建，后续 Stage 使用精确文件清单，不从任意 PATH 搜索 DLL。
$taskBin=Join-Path $taskBuild 'bin/Release'
$taskFiles=Get-ChildItem -LiteralPath $taskBin -File -Filter '*.dll' | ForEach-Object {
 [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
[ordered]@{schemaVersion=1;revision=$taskRevision;ggml=$taskLock.ggmlRevision;backend=$Backend;files=@($taskFiles)} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskBuild 'artifacts.json') -Encoding utf8
Write-Output "MotionBricks $Backend build completed: $taskBin"
