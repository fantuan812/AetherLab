param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$log=Join-Path $projectRoot 'Saved\Logs\FrontierSmoke.log'
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') (Join-Path $projectRoot 'AetherLab.uproject') '/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode?DevProfile=Smoke' -game -AetherV4Smoke -unattended -nullrhi -nosound -nop4 -nosplash "-abslog=$log" -stdout -FullStdOutLogOutput
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if(!(Select-String -LiteralPath $log -Pattern 'AETHER_V4_SMOKE_PASS' -Quiet)){throw 'Frontier smoke did not finish.'}
if(Select-String -LiteralPath $log -Pattern 'V4_CHECK FAIL|Log[A-Za-z0-9_]+: Error:' -Quiet){throw 'Frontier smoke reported errors.'}
Write-Output 'Frontier smoke passed.'
