# Normalized source/definition manifest; independent of Git commit timing and CRLF checkout.
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskFiles=foreach($taskDir in @('Source','Plugins/ReactiveWorld/Source','Scripts','Content/AetherCore/Definitions')){
 Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskDir) -Recurse -File | Where-Object { $_.Extension -in '.cpp','.h','.cs','.ps1','.py','.json' }
}
$taskEntries=foreach($taskFile in $taskFiles){
 $taskRelative=[IO.Path]::GetRelativePath($taskRoot,$taskFile.FullName).Replace('\','/')
 $taskText=[IO.File]::ReadAllText($taskFile.FullName).Replace("`r`n","`n")
 $taskDigest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($taskText))).ToLowerInvariant()
 "$taskRelative $taskDigest"
}
$taskManifest=($taskEntries | Sort-Object) -join "`n"
[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($taskManifest))).ToLowerInvariant()
