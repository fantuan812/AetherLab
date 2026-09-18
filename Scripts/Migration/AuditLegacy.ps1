param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [switch]$Fixture
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskSource=(Resolve-Path -LiteralPath $SourcePath).Path
$taskInfo=Get-Item -LiteralPath $taskSource
if($taskInfo.PSIsContainer -or $taskInfo.Length -lt 64 -or $taskInfo.Length -gt 16MB){throw 'Source is outside legacy file bounds'}
$taskRun=Join-Path $taskRoot ('Saved/V10Migration/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskRun | Out-Null
$taskBackup=Join-Path $taskRun 'source.sav'
# CreateNew 保证不覆盖备份；持有只读共享句柄时禁止并发写入这个源文件。
$taskInput=[IO.File]::Open($taskSource,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {
 $taskOutput=[IO.File]::Open($taskBackup,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try {$taskInput.CopyTo($taskOutput);$taskOutput.Flush($true)} finally {$taskOutput.Dispose()}
} finally {$taskInput.Dispose()}
$taskHash=(Get-FileHash -LiteralPath $taskBackup -Algorithm SHA256).Hash.ToLowerInvariant()
$taskCrc=[IO.Path]::ChangeExtension($taskSource,'crc')
if(Test-Path -LiteralPath $taskCrc){[IO.File]::Copy($taskCrc,(Join-Path $taskRun 'source.crc'),$false)}
# 解码备份而非仍可能被游戏更新的原路径；最终再次核对来源，发现变化就拒绝采用报告。
$taskReport=Join-Path $taskRun 'reader.json'
$taskArgs=@((Join-Path $taskRoot 'AetherLab.uproject'),'-run=AetherLegacyAudit',"-Source=$taskBackup","-Report=$taskReport",
 '-unattended','-nop4','-nullrhi','-nosound','-nosplash',"-abslog=$(Join-Path $taskRun 'Engine.log')")
if($Fixture){$taskArgs+='-Fixture'}
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') @taskArgs
$taskExit=$LASTEXITCODE
$taskUnchanged=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash.ToLowerInvariant() -eq $taskHash
$taskManifest=[ordered]@{phase='legacy_decode_only';source=$taskSource;sourceSha256=$taskHash;backup=$taskBackup;
 sourceUnchanged=$taskUnchanged;engineExit=$taskExit;databaseWritten=$false;finalSchemaConverted=$false}
$taskManifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRun 'manifest.json') -Encoding utf8
if($taskExit -ne 0 -or !$taskUnchanged){throw "Legacy audit failed; source and backup retained: $taskRun"}
$taskResult=Get-Content -LiteralPath $taskReport -Raw | ConvertFrom-Json
if(!$taskResult.legacyValid -or $taskResult.sourceSha256 -ne $taskHash){throw "Legacy report validation failed: $taskRun"}
Write-Output "Legacy decode PASS; backup/report: $taskRun. Final v10 conversion and activation are not performed."
