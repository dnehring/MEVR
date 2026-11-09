# SPDX-License-Identifier: MIT
# Read-only native verification after a protected session; writes its receipt in the run directory.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,[string]$BuildRestorationDirectory='')
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\state.ps1')
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$ctx=Get-Content -LiteralPath "$RunDirectory\context.json" -Raw|ConvertFrom-Json
$before=Get-Content -LiteralPath "$RunDirectory\before.json" -Raw|ConvertFrom-Json
$mismatches=@(Test-QaBackup $before)
$binaryErrors=@()
$expected=@(Get-Content -LiteralPath "$RunDirectory\build-files.json" -Raw|ConvertFrom-Json)
$receiptName='verified-final-state.json';$scope='After inner session restoration; test build still installed'
if($BuildRestorationDirectory){
 $batch=Get-Content -LiteralPath "$BuildRestorationDirectory\batch\runs.json" -Raw|ConvertFrom-Json
 if(@($batch|Where-Object {[IO.Path]::GetFullPath($_.directory) -eq [IO.Path]::GetFullPath($RunDirectory)}).Count -ne 1){throw 'Session is not a member of that restored build batch.'}
 $deployed=@(Get-Content -LiteralPath "$BuildRestorationDirectory\candidate.json" -Raw|ConvertFrom-Json)
 if($deployed.Count -ne $expected.Count){throw 'Deployment and session build manifests differ.'}
 foreach($file in $expected){if(@($deployed|Where-Object {$_.name -eq $file.name -and $_.sha256 -eq $file.sha256}).Count -ne 1){throw 'Deployment and session binaries differ.'}}
 $restoration=Get-Content -LiteralPath "$BuildRestorationDirectory\restored-build.json" -Raw|ConvertFrom-Json
 if(-not $restoration.restored){throw 'Outer build restoration did not complete.'}
 $expected=@(Get-Content -LiteralPath "$BuildRestorationDirectory\original.json" -Raw|ConvertFrom-Json)
 if($expected.Count -ne $restoration.files.Count){throw 'Restoration receipt differs from original manifest.'}
 foreach($file in $expected){if(@($restoration.files|Where-Object {$_.name -eq $file.name -and $_.sha256 -eq $file.sha256 -and $_.exists -eq $file.exists}).Count -ne 1){throw 'Restoration receipt differs from original manifest.'}}
 $receiptName='verified-after-build-restoration.json';$scope='After outer build restoration; original installation expected'
}
foreach($file in $expected){
 $live=Join-Path $ctx.GameRoot ('Game\ME1\Binaries\Win64\ASI\'+$file.name)
 $exists=Test-Path -LiteralPath $live
 if($BuildRestorationDirectory -and -not $file.exists){if($exists){$binaryErrors+=$file.name}}
 elseif(-not $exists -or (Get-FileHash -LiteralPath $live).Hash -ne $file.sha256){$binaryErrors+=$file.name}
}
$processes=@(Get-QaProcesses|Select-Object ProcessName,Id)
$lease=Test-Path -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock')
$buildLease=Test-Path -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\qa-build.lock')
$runtime=Get-QaRuntime
$passed=-not ($mismatches.Count -or $binaryErrors.Count -or $processes.Count -or $lease -or
              ($BuildRestorationDirectory -and $buildLease) -or $runtime -ne $ctx.ActiveRuntime)
@{verified=(Get-Date).ToString('o');passed=$passed;scope=$scope;verifiedFiles=$before.files.Count;mismatches=$mismatches;
 binaryMismatches=$binaryErrors;processes=$processes;leaseExists=$lease;buildLeaseExists=$buildLease;runtime=$runtime}|
 ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$RunDirectory\$receiptName"
if(-not $passed){throw "Final state differs from the protected session; see $receiptName."}
Write-Host "Verified $($before.files.Count) files, installed binaries, runtime and cleared session ownership."
