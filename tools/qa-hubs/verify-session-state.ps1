# SPDX-License-Identifier: MIT
# Read-only native verification after a protected session; writes its receipt in the run directory.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\state.ps1')
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$ctx=Get-Content -LiteralPath "$RunDirectory\context.json" -Raw|ConvertFrom-Json
$before=Get-Content -LiteralPath "$RunDirectory\before.json" -Raw|ConvertFrom-Json
$mismatches=@(Test-QaBackup $before)
$binaryErrors=@()
foreach($file in @(Get-Content -LiteralPath "$RunDirectory\build-files.json" -Raw|ConvertFrom-Json)){
 $live=Join-Path $ctx.GameRoot ('Game\ME1\Binaries\Win64\ASI\'+$file.name)
 if(-not (Test-Path -LiteralPath $live) -or (Get-FileHash -LiteralPath $live).Hash -ne $file.sha256){$binaryErrors+=$file.name}
}
$processes=@(Get-QaProcesses|Select-Object ProcessName,Id)
$lease=Test-Path -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock')
$runtime=Get-QaRuntime
$passed=-not ($mismatches.Count -or $binaryErrors.Count -or $processes.Count -or $lease -or $runtime -ne $ctx.ActiveRuntime)
@{verified=(Get-Date).ToString('o');passed=$passed;verifiedFiles=$before.files.Count;mismatches=$mismatches;
 binaryMismatches=$binaryErrors;processes=$processes;leaseExists=$lease;runtime=$runtime}|
 ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$RunDirectory\verified-final-state.json"
if(-not $passed){throw 'Final state differs from the protected session; see verified-final-state.json.'}
Write-Host "Verified $($before.files.Count) files, installed binaries, runtime and cleared session ownership."
