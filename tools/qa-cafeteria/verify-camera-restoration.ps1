# SPDX-License-Identifier: MIT
# Read-only check after the candidate runner restores the original ASI over the inner run's candidate.
param([Parameter(Mandatory)][string]$ValidationDirectory)
. "$PSScriptRoot\state.ps1"
. "$PSScriptRoot\common.ps1"
$install=Get-Content -LiteralPath "$ValidationDirectory\installation.json" -Raw|ConvertFrom-Json
$last=Get-ChildItem -LiteralPath "$ValidationDirectory\batch" -Directory -Filter 'cafeteria-*'|Sort-Object Name|Select-Object -Last 1
if(-not $last){throw 'No run manifest to verify.'}
$manifest=Get-Content -LiteralPath (Join-Path $last.FullName 'before.json') -Raw|ConvertFrom-Json
$context=Get-Content -LiteralPath (Join-Path $last.FullName 'context.json') -Raw|ConvertFrom-Json
$binary=@($manifest.files|Where-Object path -EQ $install.installed)
if($binary.Count -ne 1){throw 'Installed binary is missing from the protected manifest.'}
# Change only this in-memory expectation; retain the original inner backup/receipt unchanged.
$binary[0].sha256=$install.originalSha256
$runtime=Get-QaRuntime
$processes=@(Get-QaProcesses|Select-Object Id,ProcessName)
$requests=@(Get-ChildItem -LiteralPath (Join-Path $context.LocalAppData 'MEVR') -Filter '*.request'|Select-Object -ExpandProperty Name)
$lock=Test-Path -LiteralPath (Join-Path $context.LocalAppData 'MEVR\qa-cafeteria.lock')
$mismatches=@(Test-QaBackup $manifest)
$passed=-not ($mismatches.Count -or $processes.Count -or $requests.Count -or $lock -or $runtime -ne $manifest.runtime)
$result=@{passed=$passed;checked=(Get-Date).ToString('o');verifiedFiles=$manifest.files.Count;
    mismatches=$mismatches;processes=$processes;requests=$requests;lock=$lock;runtime=$runtime;binarySha256=$install.originalSha256}
$result|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$ValidationDirectory\final-state.json"
$result|ConvertTo-Json -Depth 5
if(-not $passed){throw 'Restoration verification failed; inspect final-state.json.'}
