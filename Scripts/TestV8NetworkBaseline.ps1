param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$Port=7797)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProject='"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'
$taskPrefix='AetherNetV8_'+[guid]::NewGuid().ToString('N').Substring(0,16)
$taskServer=$null;$taskClients=@();$taskLogs=@()
try {
    $taskServerLog=Join-Path $taskRoot "Saved\Logs\$taskPrefix-Server.log"
    $taskServer=Start-Process -FilePath $taskEditor -ArgumentList @($taskProject,'/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode','-server','-game','-AetherV4NetServer',"-AetherSavePrefix=$taskPrefix","-port=$Port",'-nullrhi','-nosound','-unattended','-nop4',"-abslog=$taskServerLog") -PassThru -WindowStyle Hidden
    $taskDeadline=(Get-Date).AddSeconds(60)
    while(!(Test-Path -LiteralPath $taskServerLog) -or !(Select-String -LiteralPath $taskServerLog -Pattern 'AETHER_V4_READY' -Quiet)) {
        if($taskServer.HasExited -or (Get-Date) -gt $taskDeadline){throw 'Baseline server startup timeout.'}
        Start-Sleep -Milliseconds 500
    }
    foreach($taskProfile in 'Alpha','Beta') {
        $taskLog=Join-Path $taskRoot "Saved\Logs\$taskPrefix-$taskProfile.log";$taskLogs+=,$taskLog
        $taskClients+=Start-Process -FilePath $taskEditor -ArgumentList @($taskProject,"127.0.0.1:$Port`?DevProfile=$taskProfile",'-game','-AetherV4NetClient','-nullrhi','-nosound','-unattended','-nop4',"-abslog=$taskLog") -PassThru -WindowStyle Hidden
    }
    $taskDeadline=(Get-Date).AddSeconds(60)
    while(@($taskClients | Where-Object {!$_.HasExited}).Count -gt 0) {
        if($taskServer.HasExited -or (Get-Date) -gt $taskDeadline){throw 'Baseline client timeout or server exit.'}
        Start-Sleep -Milliseconds 500
    }
    foreach($taskLog in $taskLogs) {
        if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_V4_NET_PASS' -Quiet) -or (Select-String -LiteralPath $taskLog -Pattern 'AETHER_V4_NET_FAIL|Fatal error:' -Quiet)){throw "Baseline failed: $taskLog"}
    }
    # These checks do not inspect the other player's replicated profile or late-join reconstruction.
    Write-Output "V8 network baseline PASS: two clients, one pickup each despite retries, local ASC ownership, shared supply flag, client simulation disabled. Logs: $taskPrefix"
} finally {
    foreach($taskClient in $taskClients){if(!$taskClient.HasExited){Stop-Process -Id $taskClient.Id}}
    if($taskServer -and !$taskServer.HasExited){Stop-Process -Id $taskServer.Id}
}
