param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$taskCase=Join-Path $taskRoot ('Saved/Automation/V10LegacyAudit/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $taskCase | Out-Null
$taskSource=Join-Path $taskCase 'Profiles.sav'
[IO.File]::Copy((Join-Path $taskRoot 'Docs/Fixtures/V9/Profiles.sav'),$taskSource,$false)
$taskBytes=[IO.File]::ReadAllBytes($taskSource)
if(-not ('AetherLegacyAuditCrc' -as [type])){
 Add-Type -TypeDefinition @'
public static class AetherLegacyAuditCrc {
 public static uint Compute(byte[] data) {
  uint crc=0xffffffff;
  foreach(byte b in data) {
   crc^=b;
   for(int i=0;i<8;i++) crc=(crc>>1)^((crc&1)!=0?0xedb88320u:0u);
  }
  return ~crc;
 }
}
'@
}
$taskCrc=Join-Path $taskCase 'Profiles.crc'
[IO.File]::WriteAllText($taskCrc,('9:'+ [AetherLegacyAuditCrc]::Compute($taskBytes)),[Text.UTF8Encoding]::new($false))
$taskHash=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
$taskAudit=Join-Path $taskRoot 'Scripts/Migration/AuditLegacy.ps1'
& $taskAudit -SourcePath $taskSource -EngineRoot $EngineRoot *> (Join-Path $taskCase 'valid.log')
# 同一数据文件换成未提交的校验侧车，必须拒绝；不使用 Fixture 豁免。
[IO.File]::WriteAllText($taskCrc,'9:0',[Text.UTF8Encoding]::new($false))
$taskRejected=$false
try { & $taskAudit -SourcePath $taskSource -EngineRoot $EngineRoot *> (Join-Path $taskCase 'invalid.log') }
catch { $taskRejected=$true }
if(!$taskRejected){throw 'Uncommitted legacy generation was accepted'}
if((Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash -ne $taskHash){throw 'Audit changed source bytes'}
$taskRuns=Get-ChildItem (Join-Path $taskRoot 'Saved/V10Migration') -Filter manifest.json -Recurse |
 ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json } |
 Where-Object source -eq $taskSource
if(@($taskRuns).Count -ne 2){throw 'Missing isolated audit manifests'}
foreach($taskRun in $taskRuns) {
 if(!$taskRun.sourceUnchanged -or (Get-FileHash -LiteralPath $taskRun.backup -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskRun.sourceSha256){
  throw 'Backup is not byte-identical to the audited source'
 }
 $taskReport=Get-Content -LiteralPath (Join-Path (Split-Path $taskRun.backup) 'reader.json') -Raw | ConvertFrom-Json
 if($taskReport.databaseWritten -or $taskReport.finalSchemaConverted){throw 'Audit claimed an unperformed conversion'}
 if($taskRun.engineExit -eq 0 -and (!$taskReport.legacyValid -or @($taskReport.profiles).Count -ne 19)){throw 'Valid report is incomplete'}
 if($taskRun.engineExit -ne 0 -and ($taskReport.legacyValid -or $taskReport.detail -ne 'Missing or mismatched committed-generation checksum')){
  throw 'Negative case failed for an unexpected reason'
 }
}
[ordered]@{passed=$true;sourcePreserved=$true;independentBackups=2;validCommittedGeneration=$true;invalidChecksumRejected=$true} |
 ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskCase 'result.json') -Encoding utf8
Write-Output "Legacy audit PASS: $taskCase"
