param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8', [switch]$Listen, [switch]$Graybox)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$map = if ($Graybox) { '/Engine/Maps/Entry?game=/Script/AetherLab.AetherAdventureMode' } else { '/Game/SwordMagic/Maps/L_BrokenBell_Playable?game=/Script/AetherLab.AetherModularAdventureMode' }
if ($Listen) { $map += '?listen' }
& $editor (Join-Path $projectRoot 'AetherLab.uproject') $map -game -windowed -ResX=1600 -ResY=1000
exit $LASTEXITCODE
