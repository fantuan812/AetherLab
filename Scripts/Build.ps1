param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',[switch]$RecordCandidate)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'AetherLab.uproject'
$buildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (!(Test-Path -LiteralPath $buildTool)) { throw "UE build tool not found: $buildTool" }
if($RecordCandidate){
 . (Join-Path $PSScriptRoot 'Validate/CandidateEvidence.ps1')
 $taskBefore=Get-AetherSourceCommit $projectRoot
}
& $buildTool AetherLabEditor Win64 Development $projectPath -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
if($LASTEXITCODE -eq 0 -and $RecordCandidate){
 if((Get-AetherSourceCommit $projectRoot) -cne $taskBefore){throw 'Source changed during build; candidate not recorded.'}
 $taskIdentity=Get-AetherArtifactIdentity $projectRoot $EngineRoot
 $taskEvidence=Join-Path $projectRoot 'Saved/Acceptance'
 [IO.Directory]::CreateDirectory($taskEvidence)|Out-Null
 [ordered]@{schema=1;candidateCommit=$taskBefore;sourceTreeClean=$true;build='passed';functionalAcceptance='not_run';releaseAccepted=$false;artifactHash=$taskIdentity.artifactHash;engineBuild=$taskIdentity.engineBuild;payload=$taskIdentity.payload}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $taskEvidence 'editor-build.json') -Encoding utf8
}
exit $LASTEXITCODE
