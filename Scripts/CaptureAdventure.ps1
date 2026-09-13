param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8', [switch]$Graybox)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = Join-Path $projectRoot 'Saved\Logs\AdventureCapture.log'
$map=if($Graybox){'/Engine/Maps/Entry?game=/Script/AetherLab.AetherAdventureMode'}else{'/Game/SwordMagic/Maps/L_BrokenBell_Playable?game=/Script/AetherLab.AetherModularAdventureMode'}
& $editor (Join-Path $projectRoot 'AetherLab.uproject') $map -game -AetherAdventureCapture -RenderOffscreen -unattended -nop4 -nosound -nosplash -windowed -ResX=1600 -ResY=1000 -ForceRes "-abslog=$log" -stdout
exit $LASTEXITCODE
