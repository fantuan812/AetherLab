param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskLock=Get-Content (Join-Path $taskRoot 'Build/ThirdParty/MotionBricks.lock.json') -Raw | ConvertFrom-Json
$taskManifest=Get-Content (Join-Path $taskRoot 'Build/ThirdParty/MotionBricks.bundle.json') -Raw | ConvertFrom-Json
$taskDestination=Join-Path $taskRoot 'Saved/ThirdParty/MotionBundle'
New-Item -ItemType Directory -Force -Path $taskDestination | Out-Null
foreach($taskEntry in $taskManifest.files){
 # 清单虽来自固定上游，也不能允许绝对路径或父目录越界写入。
 if($taskEntry.path -notmatch '^(g1-f32|styles)/[A-Za-z0-9_.-]+$'){throw "Unsafe bundle path: $($taskEntry.path)"}
 $taskPath=Join-Path $taskDestination $taskEntry.path
 $taskValid=(Test-Path -LiteralPath $taskPath) -and (Get-Item -LiteralPath $taskPath).Length -eq $taskEntry.bytes
 if($taskValid){$taskValid=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash -eq $taskEntry.sha256}
 if($taskValid){continue}
 New-Item -ItemType Directory -Force -Path (Split-Path -Parent $taskPath) | Out-Null
 $taskUri="https://huggingface.co/$($taskLock.modelRepository)/resolve/$($taskLock.modelRevision)/$($taskEntry.path)"
 Write-Output "Downloading pinned asset: $($taskEntry.path)"
 Invoke-WebRequest -Uri $taskUri -OutFile "$taskPath.partial"
 # 下载成功不代表内容正确；只有长度与 SHA256 同时匹配，才原子替换正式文件。
 if((Get-Item -LiteralPath "$taskPath.partial").Length -ne $taskEntry.bytes -or (Get-FileHash -LiteralPath "$taskPath.partial" -Algorithm SHA256).Hash -ne $taskEntry.sha256){throw "Bundle hash mismatch: $($taskEntry.path)"}
 Move-Item -LiteralPath "$taskPath.partial" -Destination $taskPath -Force
}
Write-Output "Verified pinned model/style bundle: $taskDestination"
