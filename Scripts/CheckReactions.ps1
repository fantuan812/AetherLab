param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[string]$LegacyFixture='')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherReactionCheck_'+[guid]::NewGuid().ToString('N').Substring(0,16)
$taskLog=Join-Path $taskRoot 'Saved\Logs\ReactionCheck.log'
$taskLegacyArgs=@()
if($LegacyFixture){$taskLegacyArgs+="-AetherLegacyFixture=$LegacyFixture"}
& $taskEditor (Join-Path $taskRoot 'AetherLab.uproject') '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile=ReactionCheck' -game -nullrhi -nosound -unattended -nop4 -AetherReactionCheck "-AetherSavePrefix=$taskPrefix" @taskLegacyArgs "-abslog=$taskLog"
if($LASTEXITCODE -ne 0){throw "Reaction check exited with $LASTEXITCODE"}
if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_REACTION_PASS' -Quiet)){throw "Reaction assertion failed; see $taskLog"}
Write-Output 'Finite pour, blade cut, physical support release, attribution expiry and snapshot checks passed.'
