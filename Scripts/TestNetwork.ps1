param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8', [int]$Port = 7789)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = '"' + (Join-Path $projectRoot 'AetherLab.uproject') + '"'
$logDir = Join-Path $projectRoot 'Saved\Logs'
$serverLog = Join-Path $logDir 'AetherNetServer.log'
$server = $null
$client = $null
try {
    $testStarted = Get-Date
    $server = Start-Process -FilePath $editor -ArgumentList @($project,'/Game/SwordMagic/Maps/L_BrokenBell_Playable?game=/Script/AetherLab.AetherModularAdventureMode','-server','-game','-AetherNetServer',"-port=$Port",'-unattended','-nullrhi','-nosound','-nop4',"-abslog=$serverLog") -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(60)
    while (!(Test-Path -LiteralPath $serverLog) -or (Get-Item -LiteralPath $serverLog).LastWriteTime -lt $testStarted -or !(Select-String -LiteralPath $serverLog -Pattern 'AETHER_ADVENTURE_READY' -Quiet)) {
        if ($server.HasExited -or (Get-Date) -gt $deadline) { throw 'Local test server failed to start.' }
        Start-Sleep -Milliseconds 500
    }
    # The second process joins AFTER the first exits. It must receive the already-frozen baseline.
    foreach ($index in 1,2) {
        $clientLog = Join-Path $logDir "AetherNetClient$index.log"
        $clientStarted = Get-Date
        $client = Start-Process -FilePath $editor -ArgumentList @($project,"127.0.0.1:$Port",'-game','-AetherNetClient','-unattended','-nullrhi','-nosound','-nop4',"-abslog=$clientLog") -PassThru -WindowStyle Hidden
        if (!$client.WaitForExit(60000)) { throw "Client $index timed out." }
        if (!(Test-Path -LiteralPath $clientLog) -or (Get-Item -LiteralPath $clientLog).LastWriteTime -lt $clientStarted -or !(Select-String -LiteralPath $clientLog -Pattern 'AETHER_NET_CLIENT_PASS' -Quiet)) { throw "Client $index failed replication/authority checks." }
        if (Select-String -LiteralPath $clientLog -Pattern 'AETHER_NET_CLIENT_FAIL|Log[A-Za-z0-9_]+: Error:' -Quiet) { throw "Client $index emitted errors." }
        Write-Output "Client $index passed: world baseline, frozen collision, GAS cost, authority, equipment RPC and remote equipment visuals."
        $client = $null
    }
    Write-Output 'Local dedicated server and late join verification passed.'
} finally {
    if ($client -and !$client.HasExited) { Stop-Process -Id $client.Id }
    if ($server -and !$server.HasExited) { Stop-Process -Id $server.Id }
}
