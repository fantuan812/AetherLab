param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskPrefix='AetherData_'+[guid]::NewGuid().ToString('N').Substring(0,16)
$taskLog=Join-Path $taskRoot "Saved\Logs\$taskPrefix.log"
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProcess=Start-Process -FilePath $taskEditor -ArgumentList @('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"','/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile=DataCheck','-game','-nullrhi','-nosound','-unattended','-nop4','-AetherDataCheck',"-AetherSavePrefix=$taskPrefix","-abslog=$taskLog") -PassThru -WindowStyle Hidden
try {
 if(!$taskProcess.WaitForExit(60000)){throw 'Data check timed out'}
 if($taskProcess.ExitCode -ne 0 -or !(Select-String -LiteralPath $taskLog -Pattern 'AETHER_DATA_PASS failures=0' -Quiet)){throw "Data check failed: $taskLog"}
} finally {if(!$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
Write-Output "Data contracts passed: $taskLog"
