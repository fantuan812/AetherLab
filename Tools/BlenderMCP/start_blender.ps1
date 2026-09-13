$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$blenderExe = 'E:\steam\steamapps\common\Blender\blender.exe'
$blendFile = Join-Path $taskRoot 'Art\AetherLab\AetherLab_Prototype.blend'
$env:DISABLE_TELEMETRY = 'true'
if (Test-Path -LiteralPath $blendFile) {
    Start-Process -FilePath $blenderExe -ArgumentList ('"' + $blendFile + '"') -WindowStyle Hidden
} else {
    Start-Process -FilePath $blenderExe -WindowStyle Hidden
}
