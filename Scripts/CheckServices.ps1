param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherService_'+[guid]::NewGuid().ToString('N').Substring(0,16)
foreach($taskPhase in 'Fail','Retry','Reload') {
    $taskLog=Join-Path $taskRoot "Saved\Logs\$taskPrefix-$taskPhase.log"
    $taskArgs=@('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"','/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherLab.AetherFrontierMode?DevProfile=ServiceCheck','-game','-nullrhi','-nosound','-unattended','-nop4','-AetherServiceCheck',"-AetherServicePhase=$taskPhase","-AetherSavePrefix=$taskPrefix","-abslog=$taskLog")
    $taskProcess=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
    try {
        if(!$taskProcess.WaitForExit(60000)){throw "Service $taskPhase timed out"}
        if($taskProcess.ExitCode -ne 0 -or !(Select-String -LiteralPath $taskLog -Pattern "AETHER_SERVICE_PASS phase=$taskPhase failures=0" -Quiet)){throw "Service $taskPhase failed: $taskLog"}
    } finally {if(!$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
    Write-Output "Service phase $taskPhase passed: $taskLog"
}
Write-Output "Service transactions passed across three short process starts; fixture $taskPrefix"
