param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') (Join-Path $projectRoot 'AetherLab.uproject') '/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode?DevProfile=Capture' -game -AetherV4Capture -AetherSavePrefix=AetherCapture_v4 -RenderOffscreen -ForceRes -windowed -ResX=1600 -ResY=1000 -unattended -nosound -nop4 "-abslog=$projectRoot\Saved\Logs\V4-Capture.log" -stdout
exit $LASTEXITCODE
