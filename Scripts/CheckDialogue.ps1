param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$TimeoutSeconds=210)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherDialogueCheck_'+[guid]::NewGuid().ToString('N')
$taskLog=Join-Path $taskRoot ('Saved\Logs\'+$taskPrefix+'.log')
$taskArgs=@(('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),'/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode','-game','-nullrhi','-nosound','-unattended','-nop4','-AetherDialogueCheck',"-AetherSavePrefix=$taskPrefix",('-abslog="'+$taskLog+'"'))
$taskProcess=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -PassThru
try {
 if(!$taskProcess.WaitForExit($TimeoutSeconds*1000)){throw "Dialogue probe timed out: $taskLog"}
 if($taskProcess.ExitCode -ne 0){throw "Dialogue check exited with $($taskProcess.ExitCode): $taskLog"}
 if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_DIALOGUE_PASS.*scope=local_dialogue_service.*synthetic_setup=true' -Quiet)){throw "Current dialogue lifecycle assertion failed: $taskLog"}
} finally {if(!$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
Write-Output 'Production dialogue lifecycle and persistent service-after-close checks passed. Setup and damage serial were controlled fixtures; no visual, splitscreen, or replicated damage acceptance is implied.'
