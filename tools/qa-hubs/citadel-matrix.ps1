# SPDX-License-Identifier: MIT
# Six independent launch/capture/restore cycles, alternating menu workload order.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$PackageDirectory,[string]$PythonExe='python',
 [string]$OutputDirectory='', [ValidateRange(30,120)][int]$CaptureSeconds=90)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$package=[IO.Path]::GetFullPath($PackageDirectory)
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo ('scratch\qa-hubs\matrix-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$output=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $output){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $output|Out-Null
$order=@('Quality','Balanced','Performance','Performance','Balanced','Quality')
@{workloads=$order;seconds=$CaptureSeconds;eye=@(2284,2448);sweep=120;package=$package;
 budgetsMs=@((1000/72),(1000/90),(1000/120));severeIntervalMs=2000/90;severeHookMs=2000/90}|
 ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$output\plan.json"
$runs=@();$baselineBuild=$null
Start-Transcript -LiteralPath "$output\transcript.txt"|Out-Null
try{
 foreach($workload in $order){
  $previous=@(Get-ChildItem -LiteralPath "$package\sessions" -Directory -ErrorAction SilentlyContinue).FullName
  & "$PSScriptRoot\citadel-session.ps1" -Action Measure -FakeHeadset -PackageDirectory $package `
   -PythonExe $PythonExe -EyeWidth 2284 -EyeHeight 2448 -SweepDegrees 120 -CaptureSeconds $CaptureSeconds -Workload $workload
  $new=@(Get-ChildItem -LiteralPath "$package\sessions" -Directory|Where-Object {$_.FullName -notin $previous})
  if($new.Count -ne 1){throw 'Could not uniquely identify completed session.'}
  $run=$new[0].FullName
  $receipt=Get-Content -LiteralPath "$run\session-result.json" -Raw|ConvertFrom-Json
  if(-not $receipt.completed -or -not $receipt.restored){throw 'Session did not complete and restore.'}
  & "$PSScriptRoot\verify-session-state.ps1" -RunDirectory $run
  & "$PSScriptRoot\analyze-session.ps1" -RunDirectory $run -PythonExe $PythonExe
  $build=@(Get-Content -LiteralPath "$run\build-files.json" -Raw|ConvertFrom-Json|Sort-Object name)|ConvertTo-Json -Compress
  if($baselineBuild -and $baselineBuild -ne $build){throw 'Installed build changed across matrix.'}
  $baselineBuild=$build
  $runs+=@{workload=$workload;directory=$run}
  $runs|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$output\runs.json"
  & $PythonExe "$PSScriptRoot\compare-matrix.py" $output --partial
  if($LASTEXITCODE){throw 'Measurement validity failed; stop before another launch.'}
 }
 & $PythonExe "$PSScriptRoot\compare-matrix.py" $output
 if($LASTEXITCODE){throw 'Matrix validity failed.'}
}finally{Stop-Transcript|Out-Null}
