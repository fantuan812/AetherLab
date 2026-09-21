param(
 [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
 [int]$Width=1280,
 [int]$Height=720
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskPrefix='AetherV10Menu_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskLog=Join-Path $taskRoot "Saved/Logs/$taskPrefix.log"
$taskImages=Join-Path $taskRoot "Saved/Automation/$taskPrefix"
New-Item -ItemType Directory -Path $taskImages -Force | Out-Null
$taskProcess=$null
try {
 $taskArgs=@(
  ('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),
  '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode',
  '-game','-AetherV10MenuCapture',"-AetherSavePrefix=$taskPrefix",
  ('-AetherMenuCaptureDir="'+$taskImages+'"'),
  '-RenderOffscreen','-ForceRes','-windowed',"-ResX=$Width","-ResY=$Height",
  '-unattended','-nosound','-nop4','-NoVSync',
  '-ExecCmds="t.MaxFPS 30,r.ScreenPercentage 75"',"-abslog=$taskLog")
 $taskProcess=Start-Process -FilePath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
 $taskDeadline=(Get-Date).AddSeconds(90)
 while(!$taskProcess.HasExited){
  if((Get-Date) -gt $taskDeadline){throw "Menu capture timeout: $taskLog"}
  Start-Sleep -Milliseconds 500
 }
 $taskProcess.WaitForExit()
 if($taskProcess.ExitCode -ne 0 -or !(Select-String -LiteralPath $taskLog -Pattern 'V10_MENU_INTERACTION_PASS' -Quiet)){
  throw "Menu interaction failed: $taskLog"
 }
 if(Select-String -LiteralPath $taskLog -Pattern 'V10_MENU_INTERACTION_FAIL|Fatal error:' -Quiet){throw "Menu interaction failed: $taskLog"}
 foreach($taskImage in @('Inventory.png','Journal.png','Map.png','NewPawn.png','Skills.png','Party.png','Settings.png')){
  $taskPath=Join-Path $taskImages $taskImage
  if(!(Test-Path -LiteralPath $taskPath) -or (Get-Item -LiteralPath $taskPath).LastWriteTime -lt $taskProcess.StartTime){throw "Missing fresh capture: $taskPath"}
  # Windows 远程桌面可能把大窗口限制为桌面可用尺寸；验 PNG 实际尺寸，不能只信 -ResX/-ResY。
  $taskBytes=[IO.File]::ReadAllBytes($taskPath)
  if($taskBytes.Length -lt 24 -or [BitConverter]::ToString($taskBytes,0,8) -ne '89-50-4E-47-0D-0A-1A-0A'){throw "Invalid PNG: $taskPath"}
  [Array]::Reverse($taskBytes,16,4);[Array]::Reverse($taskBytes,20,4)
  $taskActualWidth=[BitConverter]::ToInt32($taskBytes,16);$taskActualHeight=[BitConverter]::ToInt32($taskBytes,20)
  if($taskActualWidth -ne $Width -or $taskActualHeight -ne $Height){throw ("Wrong capture resolution: {0}x{1}, expected {2}x{3}; {4}" -f $taskActualWidth,$taskActualHeight,$Width,$Height,$taskPath)}
 }
 Write-Output "Menu interaction PASS; $Width x $Height; $taskImages; $taskLog"
} finally {if($taskProcess -and !$taskProcess.HasExited){Stop-Process -Id $taskProcess.Id}}
