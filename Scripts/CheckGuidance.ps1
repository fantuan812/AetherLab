param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$TimeoutSeconds=210)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskPrefix='AetherGuideCheck_'+[guid]::NewGuid().ToString('N')
$taskLog=Join-Path $taskRoot ('Saved\Logs\'+$taskPrefix+'.log')
$taskArgs=@(('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),'/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode','-game','-nullrhi','-nosound','-unattended','-nop4','-AetherGuidanceCheck',"-AetherSavePrefix=$taskPrefix",('-abslog="'+$taskLog+'"'))
$taskProcess=Start-Process -FilePath $taskEditor -ArgumentList $taskArgs -PassThru
try {
 if(!$taskProcess.WaitForExit($TimeoutSeconds*1000)){throw "Guidance probe timed out: $taskLog"}
 if($taskProcess.ExitCode -ne 0){throw "Guidance check exited with $($taskProcess.ExitCode): $taskLog"}
 if(!(Select-String -LiteralPath $taskLog -Pattern 'AETHER_GUIDANCE_PASS.*scope=current_snapshot_service.*synthetic_setup=true' -Quiet)){throw "Current-snapshot guidance assertion failed: $taskLog"}
} finally {if(!$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
Write-Output 'Current owner-snapshot guidance, native rejection/rescue and real reaction checks passed. Prerequisites were synthetic; this is not full mainline or rendered UI acceptance.'
