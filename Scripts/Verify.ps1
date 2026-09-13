param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
foreach ($name in 'Test','Smoke','SmokeAdventure','SmokeEquipment','TestNetwork') {
    Write-Output "Running $name..."
    $log = Join-Path $projectRoot "Saved\Logs\Verify-$name-console.log"
    & (Join-Path $PSScriptRoot "$name.ps1") -EngineRoot $EngineRoot *> $log
    if ($LASTEXITCODE -ne 0) { throw "$name failed. See $log" }
    Write-Output "$name passed."
}
& (Join-Path $PSScriptRoot 'SmokeAdventure.ps1') -EngineRoot $EngineRoot -Art *> (Join-Path $projectRoot 'Saved\Logs\Verify-ArtAdventure-console.log')
if ($LASTEXITCODE -ne 0) { throw 'Art adventure integration failed.' }
Write-Output 'Art adventure passed.'
Write-Output 'AETHER_VERIFY_PASS'
