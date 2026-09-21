param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [string]$Python='python',
 [ValidateSet('CPU','Vulkan')][string]$Backend='CPU',
 [string]$SourceProject='',
 [ValidateSet('All','PrepareMotionAssets','PrepareAnimationBlueprints','ExportCrouchStyles','PrepareV10Equipment','PrepareCharacterPreview','PrepareV10Input','PrepareV10UI')][string]$StartAt='All'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if(!$SourceProject){$SourceProject=$taskRoot}
$taskRun=Join-Path $taskRoot ('Saved/Authoring/V10-'+[guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($taskRun)|Out-Null
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$taskProject=Join-Path $taskRoot 'AetherLab.uproject'
$taskStage=Join-Path $taskRoot 'Plugins/AetherMotion/Binaries/ThirdParty/Win64'
$taskSources=Join-Path $taskRoot 'ContentSource/Motion'
[IO.Directory]::CreateDirectory((Join-Path $taskSources 'Clips'))|Out-Null
[IO.Directory]::CreateDirectory((Join-Path $taskSources 'Styles'))|Out-Null
$taskResumeReached=$StartAt -eq 'All'
function Invoke-EditorAuthor([string]$Name,[string]$Marker){
 if(!$script:taskResumeReached){if($Name -eq $StartAt){$script:taskResumeReached=$true}else{return}}
 $taskScript=Join-Path $PSScriptRoot ($Name+'.py')
 $taskLog=Join-Path $taskRun ($Name+'.log')
 & $taskEditor $taskProject -run=pythonscript "-script=$taskScript" '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -unattended -nop4 -nullrhi -nosound "-abslog=$taskLog"
 if($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath $taskLog -SimpleMatch $Marker)){throw "Asset author failed: $Name; $taskLog"}
 Write-Output "Authored $Name; validation remains pending."
}
& (Join-Path $taskRoot 'Scripts/Build/StageMotionRuntime.ps1') -Backend $Backend -SourceProject $SourceProject
$taskAuthor=Join-Path $PSScriptRoot 'MotionAuthor.py'
if($StartAt -eq 'All'){
& $Python $taskAuthor extract --stage $taskStage --output (Join-Path $taskSources 'G1Skeleton.json')
if($LASTEXITCODE -ne 0){throw 'G1 skeleton extraction failed.'}
foreach($taskStyle in @('idle','walk','injured_walk','walk_boxing','walk_left','walk_right')){
 $taskSpeed=if($taskStyle -eq 'idle'){0}else{1}
 & $Python $taskAuthor bake --stage $taskStage --style $taskStyle --seconds 1 --speed $taskSpeed --output (Join-Path $taskSources ("Clips/$taskStyle.json"))
 if($LASTEXITCODE -ne 0){throw "Native clip author failed: $taskStyle"}
}
}
Invoke-EditorAuthor 'PrepareControlledAnimations' 'V10_CONTROLLED_ANIMATIONS_AUTHORED'
Invoke-EditorAuthor 'PrepareMotionAssets' '动作资源制作完成'
Invoke-EditorAuthor 'PrepareAnimationBlueprints' 'V10_ANIMATION_BLUEPRINTS_AUTHORED'
Invoke-EditorAuthor 'ExportCrouchStyles' 'V10_CROUCH_POSES_EXPORTED'
if($taskResumeReached){
foreach($taskStyle in @('crouch_idle','crouch')){
 $taskSpeed=if($taskStyle -eq 'crouch_idle'){0}else{1.5}
 & $Python $taskAuthor style --stage $taskStage --source (Join-Path $taskSources ("Clips/$taskStyle.json")) --style $taskStyle --speed $taskSpeed --output (Join-Path $taskSources ("Styles/$taskStyle.mbstyle"))
 if($LASTEXITCODE -ne 0){throw "Crouch style conversion failed: $taskStyle"}
}
& (Join-Path $taskRoot 'Scripts/Build/StageMotionRuntime.ps1') -Backend $Backend -SourceProject $SourceProject
Invoke-EditorAuthor 'PrepareMotionAssets' '动作资源制作完成'
}
Invoke-EditorAuthor 'PrepareV10Equipment' 'V10_EQUIPMENT_ASSETS_AUTHORED'
Invoke-EditorAuthor 'PrepareCharacterPreview' '角色预览资产已制作'
Invoke-EditorAuthor 'PrepareV10Input' 'V10_INPUT_AUTHORED'
Invoke-EditorAuthor 'PrepareV10UI' 'V10_UI_ASSETS_AUTHORED'
[ordered]@{schema=1;authoredAtUtc=[DateTime]::UtcNow.ToString('o');sourceCommit=(& git -C $taskRoot rev-parse HEAD);backend=$Backend;startAt=$StartAt;logs=$taskRun;validation='pending'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $taskRun 'result.json') -Encoding utf8
Write-Output "V10 asset authoring finished: $taskRun. Compile/runtime/visual acceptance is separate."
