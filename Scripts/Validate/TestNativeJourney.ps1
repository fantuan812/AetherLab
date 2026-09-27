param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'CandidateEvidence.ps1')
$taskCommit=Get-AetherSourceCommit $taskRoot
$taskBuild=Get-Content -LiteralPath (Join-Path $taskRoot 'Saved/Acceptance/editor-build.json') -Raw|ConvertFrom-Json
$taskIdentity=Get-AetherArtifactIdentity $taskRoot $EngineRoot
if($taskBuild.schema -ne 1 -or $taskBuild.build -cne 'passed' -or $taskBuild.sourceTreeClean -ne $true -or $taskBuild.candidateCommit -cne $taskCommit -or $taskBuild.artifactHash -cne $taskIdentity.artifactHash){throw 'Candidate build does not match source/binaries. Build the clean candidate using Scripts/Build.ps1 -RecordCandidate first.'}
$taskChecklist=Get-Content -LiteralPath (Join-Path $taskRoot 'Docs/Acceptance/Closure-mainline.json') -Raw|ConvertFrom-Json
$taskVersion=$taskIdentity.engineBuild
$taskEngineBuild="$($taskVersion.MajorVersion).$($taskVersion.MinorVersion).$($taskVersion.PatchVersion)-$($taskVersion.Changelist)+$($taskVersion.BranchName)"
$taskPrefix='V10Journey_'+[guid]::NewGuid().ToString('N').Substring(0,12)
$taskDir=Join-Path $taskRoot ('Saved/Automation/'+$taskPrefix)
[IO.Directory]::CreateDirectory($taskDir)|Out-Null
$taskReport=Join-Path $taskDir 'result.json'
$taskArgs=@(
 ('"'+(Join-Path $taskRoot 'AetherLab.uproject')+'"'),
 '/Game/AetherCore/Maps/L_Frontier?game=/Script/AetherGameplay.AetherFrontierMode',
 '-game','-AetherNativeJourney',("-AetherSavePrefix=$taskPrefix"),
 ("-AetherCandidateCommit=$taskCommit"),("-AetherArtifactHash=$($taskIdentity.artifactHash)"),
 ('-AetherJourneyReport="'+$taskReport+'"'),'-nullrhi','-unattended','-nosound',
 '-ExecCmds="t.MaxFPS 30"',('-abslog="'+$taskDir+'/Engine.log"')
)
$taskP=Start-Process (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') -ArgumentList $taskArgs -PassThru -WindowStyle Hidden
try{
 $taskDeadline=(Get-Date).AddSeconds(1200)
 while(!$taskP.HasExited){if((Get-Date) -gt $taskDeadline){throw "Journey deadline: $taskDir"};Start-Sleep -Milliseconds 500}
 $taskP.WaitForExit()
 if($taskP.ExitCode -ne 0 -or !(Test-Path -LiteralPath $taskReport)){throw "Journey failed: $taskDir/Engine.log"}
 $taskResult=Get-Content -LiteralPath $taskReport -Raw|ConvertFrom-Json
 Assert-AetherJourneyReport $taskResult $taskChecklist $taskCommit $taskPrefix $taskIdentity.artifactHash $taskEngineBuild
 if((Get-AetherSourceCommit $taskRoot) -cne $taskCommit -or (Get-AetherArtifactIdentity $taskRoot $EngineRoot).artifactHash -cne $taskIdentity.artifactHash){throw 'Source or binaries changed during journey.'}
 [ordered]@{schema=1;candidateCommit=$taskCommit;scope=$taskChecklist.scope;engineBuild=$taskEngineBuild;artifactHash=$taskIdentity.artifactHash;savePrefix=$taskPrefix;status='passed';renderedJourney='not_run';releaseAccepted=$false;evidence=$taskReport}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $taskDir 'evidence.json') -Encoding utf8
 Write-Output "Native service journey PASS; rendered/input journey not_run: $taskDir"
}catch{
 [ordered]@{schema=1;candidateCommit=$taskCommit;scope=$taskChecklist.scope;engineBuild=$taskEngineBuild;artifactHash=$taskIdentity.artifactHash;savePrefix=$taskPrefix;status='failed';reason=$_.Exception.Message;releaseAccepted=$false;evidence=$taskReport}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $taskDir 'evidence.json') -Encoding utf8
 throw
}finally{if(!$taskP.HasExited){Stop-Process -Id $taskP.Id}}
