# SPDX-License-Identifier: MIT
# Independent protected startup repeats; not a stationary preset benchmark.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$PackageDirectory,[string]$PythonExe='python',
 [ValidateRange(1,5)][int]$Repeats=2,[string]$OutputDirectory='',
 [ValidateSet('Splash','Flight')][string]$Phase='Splash')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$package=[IO.Path]::GetFullPath($PackageDirectory)
if(-not $OutputDirectory){$OutputDirectory=Join-Path $repo ('scratch\qa-hubs\startup-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
@{package=$package;repeats=$Repeats;seconds=30;phase=$Phase;delayAfterHostRequestSeconds=$(if($Phase -eq 'Splash'){5}else{0});
 titleHoldProcessSeconds=60;workload='Quality';foreground=$true;eye=@(2284,2448)}|
 ConvertTo-Json|Set-Content -LiteralPath "$OutputDirectory\plan.json"
$runs=@();$baselineBuild=$null
Start-Transcript -LiteralPath "$OutputDirectory\transcript.txt"|Out-Null
try{
 for($index=0;$index -lt $Repeats;$index++){
  $previous=@(Get-ChildItem -LiteralPath "$package\sessions" -Directory -ErrorAction SilentlyContinue).FullName
  & "$PSScriptRoot\citadel-session.ps1" -Action Startup -FakeHeadset -PackageDirectory $package `
   -PythonExe $PythonExe -CaptureSeconds 30 -EyeWidth 2284 -EyeHeight 2448 -Workload Quality -StartupPhase $Phase
  $new=@(Get-ChildItem -LiteralPath "$package\sessions" -Directory|Where-Object {$_.FullName -notin $previous})
  if($new.Count -ne 1){throw 'Could not uniquely identify startup session.'}
  $run=$new[0].FullName
  $receipt=Get-Content -LiteralPath "$run\session-result.json" -Raw|ConvertFrom-Json
  if(-not $receipt.completed -or -not $receipt.restored){throw 'Startup did not complete and restore.'}
  & "$PSScriptRoot\verify-session-state.ps1" -RunDirectory $run
  & "$PSScriptRoot\analyze-session.ps1" -RunDirectory $run -PythonExe $PythonExe
  & $PythonExe "$PSScriptRoot\analyze-startup.py" $run
  if($LASTEXITCODE){throw 'Startup phase analysis failed.'}
  $build=@(Get-Content -LiteralPath "$run\build-files.json" -Raw|ConvertFrom-Json|Sort-Object name)|ConvertTo-Json -Compress
  if($baselineBuild -and $build -ne $baselineBuild){throw 'Installed build changed between startup repeats.'}
  $baselineBuild=$build
  $runs+=@{repeat=$index+1;directory=$run}
  $runs|ConvertTo-Json -Depth 3|Set-Content -LiteralPath "$OutputDirectory\runs.json"
 }
}finally{Stop-Transcript|Out-Null}
