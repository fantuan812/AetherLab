param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8', [switch]$DataOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$editor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$extra=@();if($DataOnly){$extra+='-AetherDataOnly'}
& $editor (Join-Path $root 'AetherLab.uproject') -run=pythonscript "-script=$root\Tools\BlenderMCP\import_modular_unreal.py" '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' '-ini:Engine:[ConsoleVariables]:Interchange.FeatureFlags.Import.FBX=0' @extra -unattended -nop4 -nullrhi -nosound "-abslog=$root\Saved\Logs\ModularImport.log" -stdout
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
$result=Get-Content -LiteralPath (Join-Path $root 'Art\SwordMagic\Modular\unreal-import-report.json') -Raw | ConvertFrom-Json
if($result.stage -ne 'complete' -or $result.errors.Count){throw 'SwordMagic import did not complete.'}
