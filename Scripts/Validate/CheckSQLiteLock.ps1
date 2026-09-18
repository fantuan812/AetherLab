$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskLock=Get-Content (Join-Path $taskRoot 'Build/ThirdParty/SQLite.lock.json') -Raw|ConvertFrom-Json
foreach($taskFile in $taskLock.files){
 # Git 在 Windows checkout 时可能转换换行，因此文件锁按 UTF-8/LF 规范化。
 $taskText=[IO.File]::ReadAllText((Join-Path $taskRoot $taskFile.path)).Replace(([string][char]13+[string][char]10),([string][char]10))
 $taskDigest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($taskText))).ToLowerInvariant()
 if($taskDigest -ne $taskFile.sha256NormalizedUtf8){throw "Vendored SQLite differs from lock: $($taskFile.path)"}
}
Write-Output "SQLite source lock PASS: $($taskLock.version) $($taskLock.sourceId)"
