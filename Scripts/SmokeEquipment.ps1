param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8', [switch]$Graybox)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$mode = if ($Graybox) { 'AetherAdventureMode' } else { 'AetherModularAdventureMode' }
$log = Join-Path $projectRoot ('Saved\Logs\EquipmentSmoke-' + $mode + '.log')
& $editor (Join-Path $projectRoot 'AetherLab.uproject') "/Engine/Maps/Entry?game=/Script/AetherLab.$mode" -game -AetherEquipmentSmoke -unattended -nop4 -nullrhi -nosound -nosplash "-abslog=$log" -stdout
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (!(Select-String -LiteralPath $log -Pattern 'AETHER_EQUIPMENT_SMOKE_PASS' -Quiet)) { throw 'Equipment integration did not complete.' }
if (Select-String -LiteralPath $log -Pattern 'BCDE_CHECK FAIL|Log[A-Za-z0-9_]+: Error:' -Quiet) { throw 'Equipment integration errors.' }
Write-Output 'Equipment integration passed.'
