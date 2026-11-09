# SPDX-License-Identifier: MIT
# Reprocess completed raw probes with the current analyzers, then compare the full batch.
param([Parameter(Mandatory)][string]$BatchDirectory,[string]$PythonExe='python')
$ErrorActionPreference='Stop'
foreach($dir in Get-ChildItem -LiteralPath $BatchDirectory -Directory -Filter 'cafeteria-*'){
    if(-not (Get-ChildItem -LiteralPath $dir.FullName -Filter '*-probe.json')){continue}
    & $PythonExe "$PSScriptRoot\analyze.py" $dir.FullName|Out-File -LiteralPath (Join-Path $dir.FullName 'analysis-console.txt')
    if($LASTEXITCODE){throw "Analysis failed: $($dir.Name)"}
    & $PythonExe "$PSScriptRoot\investigate.py" $dir.FullName|Out-File -LiteralPath (Join-Path $dir.FullName 'investigation-console.txt')
    if($LASTEXITCODE){throw "Investigation failed: $($dir.Name)"}
}
& $PythonExe "$PSScriptRoot\compare.py" $BatchDirectory
if($LASTEXITCODE){throw 'Batch comparison failed; inspect comparison.json.'}
