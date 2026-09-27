# Shared evidence identity helpers. Dot sourcing does not launch a build or a test.
function Get-AetherSourceCommit([string]$Root) {
 $commit=(& git -c "safe.directory=$Root" -C $Root rev-parse HEAD)
 if($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$'){throw 'Cannot identify candidate commit.'}
 $dirty=@(& git -c "safe.directory=$Root" -C $Root status --porcelain)
 if($LASTEXITCODE -ne 0 -or $dirty.Count){throw 'Candidate evidence requires a clean source checkout; preserve personal changes in the original workspace.'}
 return $commit
}
function Get-AetherArtifactIdentity([string]$Root,[string]$EngineRoot) {
 $files=@(Get-ChildItem -LiteralPath (Join-Path $Root 'Binaries/Win64') -File | Where-Object {$_.Extension -in @('.dll','.modules','.target')} | Sort-Object FullName)
 if(!($files|Where-Object {$_.Name -eq 'UnrealEditor-AetherGameplay.dll'})){throw 'Gameplay editor module missing.'}
 if(!($files|Where-Object {$_.Name -eq 'UnrealEditor-AetherUI.dll'})){throw 'UI editor module missing.'}
 $pluginRoot=Join-Path $Root 'Plugins'
 if(Test-Path -LiteralPath $pluginRoot){$files+=@(Get-ChildItem -LiteralPath $pluginRoot -Recurse -File|Where-Object {$_.FullName -match '[/\\]Binaries[/\\]' -and $_.Extension -in @('.dll','.modules','.gguf','.mbstyle')}|Sort-Object FullName)}
 $payload=@($files|ForEach-Object { [ordered]@{path=$_.FullName.Substring($Root.Length).TrimStart('\','/').Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} })
 foreach($relative in @('Engine/Binaries/Win64/UnrealEditor-Cmd.exe','Engine/Build/Build.version')) {
  $path=Join-Path $EngineRoot $relative
  $payload+= [ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
 }
 $text=($payload|ForEach-Object {$_.path+'='+$_.sha256}) -join "`n"
 $sha=[Security.Cryptography.SHA256]::Create()
 try{$hash=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
 return [ordered]@{artifactHash=$hash;payload=$payload;engineBuild=(Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Build/Build.version') -Raw|ConvertFrom-Json)}
}
function Assert-AetherJourneyReport($Report,$Checklist,[string]$Commit,[string]$Prefix,[string]$ArtifactHash,[string]$EngineBuild) {
 foreach($name in @('schema','candidateCommit','savePrefix','artifactHash','engineBuild','scope','passed','fullMainline','steps','claims','questCount')) {
  if($null -eq $Report.PSObject.Properties[$name]){throw "Journey report missing $name"}
 }
 if($Report.schema -ne 2 -or $Report.passed -isnot [bool] -or $Report.passed -ne $true -or $Report.fullMainline -isnot [bool] -or $Report.fullMainline -ne $true){throw 'Journey did not pass the full-mainline schema contract.'}
 if($Report.candidateCommit -cne $Commit -or $Report.savePrefix -cne $Prefix -or $Report.artifactHash -cne $ArtifactHash -or $Report.engineBuild -cne $EngineBuild -or $Report.scope -cne $Checklist.scope){throw 'Journey candidate, isolated save, engine, artifact or scope mismatch.'}
 foreach($step in $Checklist.requiredSteps){if(@($Report.steps.step) -cnotcontains $step){throw "Journey missing stage: $step"}}
 foreach($claim in $Checklist.requiredClaims){if(@($Report.claims) -cnotcontains $claim){throw "Journey missing committed claim: $claim"}}
 if($Report.questCount -ne $Checklist.requiredClaims.Count -or @($Report.claims|Select-Object -Unique).Count -ne $Checklist.requiredClaims.Count){throw 'Committed mainline claim count mismatch.'}
 if(@($Report.steps).Count -ne $Checklist.requiredStageIndices.Count){throw 'Incomplete or duplicated mainline service stages.'}
 foreach($index in $Checklist.requiredStageIndices){if(@($Report.steps|Where-Object {$_.stage -eq $index}).Count -ne 1){throw "Missing or duplicated stage index: $index"}}
}
