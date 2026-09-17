param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') (Join-Path $projectRoot 'AetherLab.uproject') -run=pythonscript "-script=$PSScriptRoot\BuildPartitionMap.py" -EnablePlugins=PythonScriptPlugin -unattended -nullrhi -nosound -nop4
exit $LASTEXITCODE
