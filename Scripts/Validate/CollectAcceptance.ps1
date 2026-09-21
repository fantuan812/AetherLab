param([Parameter(Mandatory=$true)][string]$RunDirectory,[string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskRun=[IO.Path]::GetFullPath($RunDirectory)
if(!(Test-Path -LiteralPath $taskRun -PathType Container)){throw 'Explicit existing evidence directory required.'}
$taskFiles=@(Get-ChildItem -LiteralPath $taskRun -Recurse -File | Where-Object {$_.Extension -in @('.json','.png','.mp4','.log','.csv') -and $_.Name -ne 'acceptance-index.json'} | ForEach-Object {
 [ordered]@{path=$_.FullName.Substring($taskRun.Length).TrimStart([IO.Path]::DirectorySeparatorChar);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$taskReport=[ordered]@{
 schema=1;sourceCommit=(& git -C $taskRoot rev-parse HEAD);engine=$EngineRoot;createdUtc=[DateTime]::UtcNow.ToString('o');
 nativeLock=(Get-Content -LiteralPath (Join-Path $taskRoot 'Build/ThirdParty/MotionBricks.lock.json') -Raw|ConvertFrom-Json);
 cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
 gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,AdapterRAM);
 memoryBytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;
 evidence=$taskFiles;
 acceptance='not inferred from file presence; review per-case results, visual and performance gates'
}
$taskReport|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $taskRun 'acceptance-index.json') -Encoding utf8
Write-Output "Evidence index created: $taskRun/acceptance-index.json"
