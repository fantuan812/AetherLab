param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$Port=7795)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project='"'+(Join-Path $projectRoot 'AetherLab.uproject')+'"'
$prefix='AetherNetV4_'+(Get-Date -Format 'yyyyMMddHHmmss')
$server=$null;$clients=@()
try {
    foreach($round in 1,2){
        $serverLog=Join-Path $projectRoot "Saved\Logs\V4-NetServer-$round.log"
        $started=Get-Date
        $server=Start-Process -FilePath $editor -ArgumentList @($project,'/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode','-server','-game','-AetherV4NetServer',"-AetherSavePrefix=$prefix","-port=$Port",'-nullrhi','-nosound','-unattended','-nop4',"-abslog=$serverLog") -PassThru -WindowStyle Hidden
        $deadline=(Get-Date).AddSeconds(90)
        while(!(Test-Path -LiteralPath $serverLog) -or (Get-Item -LiteralPath $serverLog).LastWriteTime -lt $started -or !(Select-String -LiteralPath $serverLog -Pattern 'AETHER_V4_READY' -Quiet)){
            if($server.HasExited -or (Get-Date) -gt $deadline){throw 'Frontier server failed to start.'}
            Start-Sleep -Milliseconds 500
        }
        $clients=@();$logs=@()
        foreach($profile in 'Alpha','Beta'){
            $log=Join-Path $projectRoot "Saved\Logs\V4-Net-$round-$profile.log";$logs+=,$log
            $clients+=Start-Process -FilePath $editor -ArgumentList @($project,"127.0.0.1:$Port`?DevProfile=$profile",'-game','-AetherV4NetClient','-nullrhi','-nosound','-unattended','-nop4',"-abslog=$log") -PassThru -WindowStyle Hidden
        }
        foreach($client in $clients){if(!$client.WaitForExit(90000)){throw 'Client timed out.'}}
        foreach($log in $logs){
            if(!(Select-String -LiteralPath $log -Pattern 'AETHER_V4_NET_PASS' -Quiet)){throw "Client failed: $log"}
            if(Select-String -LiteralPath $log -Pattern 'AETHER_V4_NET_FAIL|Log[A-Za-z0-9_]+: Error:' -Quiet){throw "Client emitted errors: $log"}
        }
        Write-Output "Round $round passed: concurrent clients, owner-only profiles, persistent ASC, duplicate pickup rejection and shared late-join world facts."
        if(!$server.HasExited){Stop-Process -Id $server.Id};$server=$null
    }
    Write-Output 'AETHER_V4_NETWORK_PASS including server restart and reconnect'
} finally {
    foreach($client in $clients){if(!$client.HasExited){Stop-Process -Id $client.Id}}
    if($server -and !$server.HasExited){Stop-Process -Id $server.Id}
}
