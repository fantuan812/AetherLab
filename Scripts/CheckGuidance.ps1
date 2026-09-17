param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherGuideCheck_'+[guid]::NewGuid().ToString('N').Substring(0,16)
$taskLog=Join-Path $taskRoot 'Saved\Logs\GuidanceCheck.log'
& $taskEditor (Join-Path $taskRoot 'AetherLab.uproject') '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile=GuideCheck' -game -nullrhi -nosound -unattended -nop4 -AetherGuidanceCheck "-AetherSavePrefix=$taskPrefix" "-abslog=$taskLog"
if($LASTEXITCODE -ne 0){throw "Guidance check exited with $LASTEXITCODE"}
if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_GUIDANCE_PASS' -Quiet)){throw "Guidance assertion failed; see $taskLog"}
Write-Output 'Quest progression, shared targeting, personal ownership and real extinguish/rescue checks passed.'
