param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskReport=Join-Path $taskRoot ('Saved/Automation/Content-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($taskReport)|Out-Null
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') (Join-Path $taskRoot 'AetherLab.uproject') -run=AetherValidateV10Content -unattended -nop4 -nullrhi -nosound "-abslog=$taskReport/Content.log"
if($LASTEXITCODE -ne 0){throw "Content validation failed: $taskReport"}
if(!(Select-String -LiteralPath "$taskReport/Content.log" -SimpleMatch 'V10_CONTENT_RESULT errors=0')){throw 'Content completion marker missing.'}
Write-Output "Content PASS: $taskReport"
