param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8', [switch]$Art)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = Join-Path $projectRoot $(if($Art){'Saved\Logs\AdventureSmoke-Art.log'}else{'Saved\Logs\AdventureSmoke.log'})
$mode=if($Art){'AetherModularAdventureMode'}else{'AetherAdventureMode'}
& $editor (Join-Path $projectRoot 'AetherLab.uproject') "/Engine/Maps/Entry?game=/Script/AetherLab.$mode" -game -AetherAdventureSmoke -unattended -nop4 -nullrhi -nosound -nosplash "-abslog=$log" -stdout -FullStdOutLogOutput
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (!(Select-String -LiteralPath $log -Pattern 'AETHER_BCDE_SMOKE_PASS' -Quiet)) { throw 'Adventure integration checks did not complete.' }
if (Select-String -LiteralPath $log -Pattern 'Log[A-Za-z0-9_]+: Error:|BCDE_CHECK FAIL' -Quiet) { throw 'Adventure integration emitted errors.' }
Write-Output 'Adventure B-C-D-E integration passed.'
