param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$Profile='LocalPlayer',[switch]$Listen,[string]$Connect='')
$ErrorActionPreference='Stop'
if ($Profile -notmatch '^[A-Za-z0-9_]{1,32}$') { throw 'Profile must contain 1-32 letters, digits or underscores.' }
$projectRoot=Split-Path -Parent $PSScriptRoot
$map=if($Connect){$Connect+'?DevProfile='+$Profile}else{'/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile='+$Profile}
if($Listen){$map+='?listen'}
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') (Join-Path $projectRoot 'AetherLab.uproject') $map -game -windowed -ResX=1600 -ResY=1000
exit $LASTEXITCODE
