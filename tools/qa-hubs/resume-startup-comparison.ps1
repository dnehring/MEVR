# SPDX-License-Identifier: MIT
# Run only after the shared native session has been explicitly released. Never scheduled on a timer.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$BaselineDirectory,[Parameter(Mandatory)][string]$PackageDirectory,
 [Parameter(Mandatory)][string]$OutputDirectory,[string]$PythonExe='python')
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
Start-Transcript -LiteralPath "$OutputDirectory\commands.txt"|Out-Null
try{
 & "$PSScriptRoot\test-startup-build.ps1" -CandidateDirectory $BaselineDirectory -PackageDirectory $PackageDirectory `
  -OutputDirectory "$OutputDirectory\baseline-transition" -PythonExe $PythonExe -Phase Transition -Repeats 2
 & "$PSScriptRoot\build-startup-candidate.ps1" -OutputDirectory "$OutputDirectory\candidate" -BaselineDirectory $BaselineDirectory
 & "$PSScriptRoot\test-startup-build.ps1" -CandidateDirectory "$OutputDirectory\candidate" -PackageDirectory $PackageDirectory `
  -OutputDirectory "$OutputDirectory\candidate-transition" -PythonExe $PythonExe -Phase Transition -Repeats 2
 & "$PSScriptRoot\test-startup-build.ps1" -CandidateDirectory "$OutputDirectory\candidate" -PackageDirectory $PackageDirectory `
  -OutputDirectory "$OutputDirectory\candidate-culling" -PythonExe $PythonExe -Mode Culling
}finally{Stop-Transcript|Out-Null}
