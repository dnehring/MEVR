# SPDX-License-Identifier: MIT
# Run after the native comparison has restored the machine; keep builds outside timing captures.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$BuildDirectory,[Parameter(Mandatory)][string]$ReferenceSession,
 [Parameter(Mandatory)][string]$OutputDirectory,[string]$PythonExe='python')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$repo\tools\qa-cafeteria\common.ps1"
if(@(Get-QaProcesses).Count -or (Get-Process MSBuild,cl,link -ErrorAction SilentlyContinue)){throw 'Finish native sessions and builds before offline validation.'}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh output directory.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
Start-Transcript -LiteralPath "$OutputDirectory\validation.txt"|Out-Null
try{
 $build=[IO.Path]::GetFullPath($BuildDirectory)
 if(-not $build.StartsWith(([IO.Path]::GetFullPath("$repo\scratch")+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Use the private comparison build under scratch.'}
 $source=(Select-String -LiteralPath "$build\CMakeCache.txt" -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$').Matches[0].Groups[1].Value
 cmake -S $source -B $build -DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=ON
 if($LASTEXITCODE){throw 'Private LE2/LE3 target configuration failed.'}
 cmake --build $BuildDirectory --config Release --target MEVR-LE2 MEVR-LE3 -- /m:4 /nodeReuse:false
 if($LASTEXITCODE){throw 'LE2/LE3 build failed.'}
 & "$PSScriptRoot\test-offline.ps1" -PythonExe $PythonExe
 & "$PSScriptRoot\test-build-restoration.ps1"
 & "$PSScriptRoot\test-fake-resolution.ps1"
 & $PythonExe "$PSScriptRoot\test-matrix-validation.py" $ReferenceSession
 if($LASTEXITCODE){throw 'Matrix admission regression failed.'}
 @{passed=$true;at=[datetime]::UtcNow.ToString('o');buildDirectory=[IO.Path]::GetFullPath($BuildDirectory);
   checks=@('LE2 build','LE3 build','hub offline suite','binary restoration layouts/failures','fixed render scale restoration','matrix admission')}|
  ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$OutputDirectory\result.json"
}finally{Stop-Transcript|Out-Null}
