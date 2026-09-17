param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherV5Check_'+[guid]::NewGuid().ToString('N').Substring(0,16)
foreach($taskRun in @('Start','Reload')) {
    $taskLog=Join-Path $taskRoot ('Saved\Logs\V5Partition'+$taskRun+'.log')
    & $taskEditor (Join-Path $taskRoot 'AetherLab.uproject') '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile=V5Partition' -game -nullrhi -nosound -unattended -nop4 -AetherV5Check "-AetherSavePrefix=$taskPrefix" "-abslog=$taskLog"
    if($LASTEXITCODE -ne 0){throw "World $taskRun exited with $LASTEXITCODE"}
    if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_V5_LIGHT_PASS.*partition=1 nav=1' -Quiet)){throw "World $taskRun assertion failed; see $taskLog"}
}
Write-Output "Two short sequential world checks passed; isolated save prefix: $taskPrefix"
