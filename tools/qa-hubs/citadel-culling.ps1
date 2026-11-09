# SPDX-License-Identifier: MIT
# Six fixed-resolution captures; only the additional small-object cutoff changes.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$PackageDirectory,[string]$PythonExe='python',[string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$package=[IO.Path]::GetFullPath($PackageDirectory)
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo ('scratch\qa-hubs\culling-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$order=@(8,4,0,0,4,8)
@{experiment='small-object-culling';cullRadii=$order;workloads=@($order|ForEach-Object {"cull-${_}px"});
  seconds=90;eye=@(2284,2448);workload='Quality';sweep=120;package=$package;
  severeIntervalMs=2000/90;severeHookMs=2000/90}|
 ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$OutputDirectory\plan.json"
$runs=@()
Start-Transcript -LiteralPath "$OutputDirectory\transcript.txt"|Out-Null
try{
 foreach($radius in $order){
  $previous=@(Get-ChildItem -LiteralPath "$package\sessions" -Directory -ErrorAction SilentlyContinue).FullName
  & "$PSScriptRoot\citadel-session.ps1" -Action Measure -FakeHeadset -PackageDirectory $package -PythonExe $PythonExe `
   -CaptureSeconds 90 -EyeWidth 2284 -EyeHeight 2448 -SweepDegrees 120 -Workload Quality -CullRadiusPx $radius
  $new=@(Get-ChildItem -LiteralPath "$package\sessions" -Directory|Where-Object {$_.FullName -notin $previous})
  if($new.Count -ne 1){throw 'Could not identify completed culling session.'}
  $run=$new[0].FullName
  $receipt=Get-Content -LiteralPath "$run\session-result.json" -Raw|ConvertFrom-Json
  if(-not $receipt.completed -or -not $receipt.restored){throw 'Session did not complete and restore.'}
  & "$PSScriptRoot\verify-session-state.ps1" -RunDirectory $run
  & "$PSScriptRoot\analyze-session.ps1" -RunDirectory $run -PythonExe $PythonExe
  $runs+=@{workload="cull-${radius}px";cullPx=$radius;directory=$run}
  $runs|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$OutputDirectory\runs.json"
  & $PythonExe "$PSScriptRoot\compare-matrix.py" $OutputDirectory --partial
  if($LASTEXITCODE){throw 'Culling measurement validity failed.'}
 }
 & $PythonExe "$PSScriptRoot\compare-matrix.py" $OutputDirectory
 if($LASTEXITCODE){throw 'Culling comparison validity failed.'}
}finally{Stop-Transcript|Out-Null}
