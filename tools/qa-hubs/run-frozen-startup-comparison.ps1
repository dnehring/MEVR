# SPDX-License-Identifier: MIT
# Compare the retained baseline and candidate from prepare-settling-baseline without editing shared source.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$StageDirectory,[Parameter(Mandatory)][string]$PackageDirectory,
 [Parameter(Mandatory)][string]$OutputDirectory,[string]$PythonExe='python',[string]$BaselineDirectory='',
 [string]$BaselineBatchDirectory='')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage=[IO.Path]::GetFullPath($StageDirectory)
if(-not $BaselineDirectory){$BaselineDirectory="$stage\baseline"}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
foreach($pair in @(@('SystemSettings.cpp','src\game\SystemSettings.cpp'),@('SystemSettingsTests.cpp','tests\SystemSettingsTests.cpp'))){
 if((Get-FileHash -LiteralPath "$stage\candidate-inputs\$($pair[0])").Hash -ne (Get-FileHash -LiteralPath "$repo\$($pair[1])").Hash){throw 'Shared candidate changed since it was frozen; review before comparison.'}
}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
Start-Transcript -LiteralPath "$OutputDirectory\commands.txt"|Out-Null
try{
 if($BaselineBatchDirectory){
  $baselineBatch=[IO.Path]::GetFullPath($BaselineBatchDirectory)
  $entries=@(Get-Content -LiteralPath "$baselineBatch\runs.json" -Raw|ConvertFrom-Json)
  if($entries.Count -ne 2 -or @($entries.directory|Sort-Object -Unique).Count -ne 2){throw 'Reused baseline must contain two distinct completed captures.'}
  $expected=@(Get-Content -LiteralPath "$BaselineDirectory\build-files.json" -Raw|ConvertFrom-Json)
  foreach($entry in $entries){
   $analysis=Get-Content -LiteralPath "$($entry.directory)\startup-analysis.json" -Raw|ConvertFrom-Json
   if(-not $analysis.measurementValidity.valid){throw 'Reused baseline measurement was not admitted.'}
   $observed=@(Get-Content -LiteralPath "$($entry.directory)\build-files.json" -Raw|ConvertFrom-Json)
   if($observed.Count -ne $expected.Count){throw 'Reused baseline build manifest changed.'}
   foreach($file in $expected){if(@($observed|Where-Object {$_.name -eq $file.name -and $_.sha256 -eq $file.sha256}).Count -ne 1){throw 'Reused baseline has a different binary.'}}
  }
  @{batch=$baselineBatch;runs=$entries;reason='Retained independent baseline captures; candidate-only retry'}|
   ConvertTo-Json -Depth 4|Set-Content "$OutputDirectory\baseline-reuse.json"
 }else{
  & "$PSScriptRoot\test-startup-build.ps1" -CandidateDirectory $BaselineDirectory -PackageDirectory $PackageDirectory `
   -OutputDirectory "$OutputDirectory\baseline-transition" -PythonExe $PythonExe -Phase Transition -Repeats 2
  $baselineBatch="$OutputDirectory\baseline-transition\batch"
 }
 Copy-Item -LiteralPath "$stage\candidate-inputs\SystemSettings.cpp" -Destination "$stage\source\src\game\SystemSettings.cpp" -Force
 Copy-Item -LiteralPath "$stage\candidate-inputs\SystemSettingsTests.cpp" -Destination "$stage\source\tests\SystemSettingsTests.cpp" -Force
 & "$PSScriptRoot\build-startup-candidate.ps1" -SourceDirectory "$stage\source" -BuildDirectory "$stage\build" `
  -OutputDirectory "$OutputDirectory\candidate" -BaselineDirectory $BaselineDirectory
 & "$PSScriptRoot\test-startup-build.ps1" -CandidateDirectory "$OutputDirectory\candidate" -PackageDirectory $PackageDirectory `
  -OutputDirectory "$OutputDirectory\candidate-transition" -PythonExe $PythonExe -Phase Transition -Repeats 2
 & $PythonExe "$PSScriptRoot\compare-startup.py" $baselineBatch "$OutputDirectory\candidate-transition\batch" "$OutputDirectory\startup-comparison.json"
 if($LASTEXITCODE){throw 'Startup comparison is not controlled; inspect the comparison report.'}
 $comparison=Get-Content -LiteralPath "$OutputDirectory\startup-comparison.json" -Raw|ConvertFrom-Json
 if(-not $comparison.settlingResetRemoved){throw 'Candidate did not remove settling resets; inspect before further native tests.'}
 & "$PSScriptRoot\test-startup-build.ps1" -CandidateDirectory "$OutputDirectory\candidate" -PackageDirectory $PackageDirectory `
  -OutputDirectory "$OutputDirectory\candidate-culling" -PythonExe $PythonExe -Mode Culling
}finally{Stop-Transcript|Out-Null}
