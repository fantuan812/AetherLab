# Opt-in, small report-contract regression. This file is not run by authoring or build scripts.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'CandidateEvidence.ps1')
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskChecklist=Get-Content -LiteralPath (Join-Path $taskRoot 'Docs/Acceptance/Closure-mainline.json') -Raw|ConvertFrom-Json
$taskReport=[ordered]@{schema=2;candidateCommit=('a'*40);savePrefix='V10Journey_contract';artifactHash=('b'*64);engineBuild='fixture';scope=$taskChecklist.scope;passed=$true;fullMainline=$true;questCount=$taskChecklist.requiredClaims.Count;claims=$taskChecklist.requiredClaims;steps=@()}
for($i=0;$i -lt $taskChecklist.requiredStageIndices.Count;$i++){
 $taskReport.steps+=@{stage=$i;step=$(if($i -lt $taskChecklist.requiredSteps.Count){$taskChecklist.requiredSteps[$i]}else{"fixture_$i"})}
}
$taskJson=$taskReport|ConvertTo-Json -Depth 8
function Assert-Report($Report){Assert-AetherJourneyReport $Report $taskChecklist ('a'*40) 'V10Journey_contract' ('b'*64) 'fixture'}
Assert-Report ($taskJson|ConvertFrom-Json)
$taskMutations=@(
 {param($r)$r.fullMainline=$false},
 {param($r)$r.PSObject.Properties.Remove('schema')},
 {param($r)$r.candidateCommit='wrong'},
 {param($r)$r.savePrefix='personal'},
 {param($r)$r.scope='menu_smoke'},
 {param($r)$r.artifactHash='wrong'},
 {param($r)$r.engineBuild='wrong'},
 {param($r)$r.claims=@('Q_Main_01')},
 {param($r)$r.steps=@($r.steps|Select-Object -Skip 1)},
 {param($r)$r.steps[0].stage=1},
 {param($r)$r.passed='true'}
)
foreach($mutate in $taskMutations){
 $r=$taskJson|ConvertFrom-Json
 & $mutate $r
 $rejected=$false
 try{Assert-Report $r}catch{$rejected=$true}
 if(!$rejected){throw 'Report contract accepted invalid evidence.'}
}
Write-Output 'Report contract: valid fixture accepted; 11 malformed/mismatched fixtures rejected. This does not run or accept gameplay.'
