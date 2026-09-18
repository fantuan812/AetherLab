param(
    [string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',
    [switch]$Network,
    [switch]$World,
    [switch]$Services,
    [switch]$DataContracts,
    [switch]$Guidance
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskRegistry=Get-Content -LiteralPath (Join-Path $taskRoot 'Docs\Tests-v8.json') -Raw | ConvertFrom-Json
$taskSha=(& git -c "safe.directory=$($taskRoot.Replace('\','/'))" -C $taskRoot rev-parse HEAD).Trim()
$taskDirty=@(& git -c "safe.directory=$($taskRoot.Replace('\','/'))" -C $taskRoot status --porcelain)
$taskBuild=Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
$taskRun='V8-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6)
$taskFolder=Join-Path $taskRoot "Saved\Automation\$taskRun"
New-Item -ItemType Directory -Path $taskFolder -Force | Out-Null
$taskSummary=[ordered]@{
    CommitSha=$taskSha; WorktreeDirtyAtStart=($taskDirty.Count -gt 0); ChangedPaths=$taskDirty
    EngineBuild="$($taskBuild.MajorVersion).$($taskBuild.MinorVersion).$($taskBuild.PatchVersion)-$($taskBuild.Changelist)"
    Target='AetherLabEditor Win64 Development'; FixtureVersion=$taskRegistry.fixtureVersion
    StartedUtc=(Get-Date).ToUniversalTime().ToString('o'); ArtifactLocation="Saved/Automation/$taskRun"
    Command=@(); Cases=@(); Passed=0; Failed=0; Skipped=0; NotRun=@('Scale4096','rendered-play','full-RX-acceptance','cook-hlod','four-player-stress','V8-07-network-acceptance','world-reactions','network-baseline','world-services','world-data-contracts','world-guidance')
}
# Fingerprint inputs when dirty: a parent SHA alone must not masquerade as the tested revision.
$taskInputs=@(& git -c "safe.directory=$($taskRoot.Replace('\','/'))" -C $taskRoot ls-files --cached --others --exclude-standard Source Plugins Content Config Scripts Docs '*.uproject')
$taskSummary.InputHashes=@($taskInputs | Where-Object {Test-Path -LiteralPath (Join-Path $taskRoot $_) -PathType Leaf} | ForEach-Object {[ordered]@{Path=$_;SHA256=(Get-FileHash -LiteralPath (Join-Path $taskRoot $_) -Algorithm SHA256).Hash}})
try {
    $taskNames=@($taskRegistry.cases | Where-Object {$_.default} | ForEach-Object {$_.id})
    $taskFilter=$taskNames -join '+'
    $taskEditor=Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $taskSummary.Command+="UnrealEditor-Cmd AetherLab.uproject -nullrhi -ExecCmds=`"Automation RunTests $taskFilter`" -TestExit=`"Automation Test Queue Empty`""
    & $taskEditor (Join-Path $taskRoot 'AetherLab.uproject') -unattended -nop4 -nullrhi -nosound -nosplash "-ExecCmds=Automation RunTests $taskFilter" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskFolder\Rules" "-abslog=$taskFolder\Rules.log"
    if($LASTEXITCODE -ne 0){throw "Rules process exited $LASTEXITCODE"}
    $taskReport=Get-Content -LiteralPath (Join-Path $taskFolder 'Rules\index.json') -Raw | ConvertFrom-Json
    foreach($taskName in $taskNames){
        $taskCase=@($taskReport.tests | Where-Object {$_.fullTestPath -eq $taskName})
        $taskState=if($taskCase.Count -eq 1){$taskCase[0].state}else{'Missing'}
        $taskSummary.Cases+=[ordered]@{Id=$taskName;Result=$taskState}
        if($taskState -eq 'Success'){$taskSummary.Passed++}else{$taskSummary.Failed++}
    }
    if($taskReport.failed -gt 0 -or $taskSummary.Failed -gt 0){throw 'Rule failure or missing registered case'}
    foreach($taskCheck in @(@{Enabled=$World;Script='CheckReactions.ps1';Id='world-reactions'},@{Enabled=$Network;Script='TestV8NetworkBaseline.ps1';Id='network-baseline'},@{Enabled=$Services;Script='CheckServices.ps1';Id='world-services'},@{Enabled=$DataContracts;Script='CheckDataContracts.ps1';Id='world-data-contracts'},@{Enabled=$Guidance;Script='CheckGuidance.ps1';Id='world-guidance'})){
        if(!$taskCheck.Enabled){continue}
        $taskExtra=@(if($taskCheck.Id -eq 'network-baseline' -and $DataContracts){'-DataDefinitions'})
        $taskSummary.Command+="Scripts/$($taskCheck.Script) $($taskExtra -join ' ')"
        $taskStart=Get-Date
        # A child PowerShell keeps scripts using exit from terminating this report writer.
        & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $PSScriptRoot $taskCheck.Script) -EngineRoot $EngineRoot @taskExtra
        $taskPassed=($LASTEXITCODE -eq 0)
        $taskSummary.NotRun=@($taskSummary.NotRun | Where-Object {$_ -ne $taskCheck.Id})
        $taskSummary.Cases+=[ordered]@{Id=$taskCheck.Id;Result=$(if($taskPassed){'Success'}else{'Fail'});Seconds=((Get-Date)-$taskStart).TotalSeconds}
        if($taskPassed){$taskSummary.Passed++}else{$taskSummary.Failed++;throw "Failed $($taskCheck.Id)"}
    }
} catch {
    $taskSummary.Failed=[Math]::Max(1,$taskSummary.Failed);$taskSummary.Error=$_.Exception.Message
    throw
} finally {
    $taskSummary.FinishedUtc=(Get-Date).ToUniversalTime().ToString('o')
    $taskSummary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $taskFolder 'summary.json') -Encoding utf8
    Write-Output "V8 report: $taskFolder\summary.json"
}
