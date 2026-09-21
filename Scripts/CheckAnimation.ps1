param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherAnimCheck_'+[guid]::NewGuid().ToString('N').Substring(0,16)
$taskLog=Join-Path $taskRoot 'Saved\Logs\AnimationCheck.log'
& $taskEditor (Join-Path $taskRoot 'AetherLab.uproject') '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode' -game -nullrhi -nosound -unattended -nop4 -AetherAnimationCheck '-ExecCmds=aether.Motion.Backend 0' "-AetherSavePrefix=$taskPrefix" "-abslog=$taskLog"
if($LASTEXITCODE -ne 0){throw "Animation check exited with $LASTEXITCODE"}
if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_ANIMATION_PASS' -Quiet)){throw "Animation assertion failed; see $taskLog"}
Write-Output 'Native graph, jump/land states, montage evaluation and timing check passed.'
