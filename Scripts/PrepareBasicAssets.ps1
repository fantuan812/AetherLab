param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$source=Join-Path $EngineRoot 'Templates\TemplateResources\High\Characters\Content\Mannequins'
$destination=Join-Path $projectRoot 'Content\Characters\Mannequins'
if(!(Test-Path -LiteralPath $destination)){New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null;Copy-Item -LiteralPath $source -Destination $destination -Recurse}
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') (Join-Path $projectRoot 'AetherLab.uproject') -run=pythonscript "-script=$PSScriptRoot\PrepareBasicAssets.py" -EnablePlugins=PythonScriptPlugin -unattended -nullrhi -nosound -nop4
exit $LASTEXITCODE
