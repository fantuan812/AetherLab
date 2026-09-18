param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskPrefix='AetherV806_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskLog=Join-Path $taskRoot "Saved/Logs/$taskPrefix.log"
$taskEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
New-Item -ItemType Directory -Path (Join-Path $taskRoot 'Saved/Automation') -Force | Out-Null
$taskProcess=$null
try {
 $taskProcess=Start-Process -FilePath $taskEditor -ArgumentList @(('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),'/Engine/Maps/Entry?game=/Script/AetherLab.AetherFrontierMode','-game','-AetherV806Capture',"-AetherSavePrefix=$taskPrefix",'-windowed','-ResX=1280','-ResY=800','-unattended','-nosound','-nop4','-NoVSync','-ExecCmds="t.MaxFPS 30,r.ScreenPercentage 75"',"-abslog=$taskLog") -PassThru -WindowStyle Hidden
 $taskDeadline=(Get-Date).AddSeconds(90)
 while(!$taskProcess.HasExited){if((Get-Date) -gt $taskDeadline){throw "Render capture timeout: $taskLog"};Start-Sleep -Milliseconds 500}
 $taskProcess.WaitForExit();if($taskProcess.ExitCode -ne 0){throw "Render exited $($taskProcess.ExitCode): $taskLog"}
 foreach($taskImage in 'Interaction','Inventory','Quests'){
  $taskPath=Join-Path $taskRoot "Saved/Automation/V806-$taskImage.png"
  if(!(Test-Path -LiteralPath $taskPath) -or (Get-Item -LiteralPath $taskPath).LastWriteTime -lt $taskProcess.StartTime){throw "Missing fresh capture: $taskPath"}
  Write-Output $taskPath
 }
} finally {if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
