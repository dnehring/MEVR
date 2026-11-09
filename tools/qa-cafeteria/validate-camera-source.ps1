# SPDX-License-Identifier: MIT
# Reproduce source-build and offline checks before a private camera-diagnostic candidate run.
#Requires -Version 7.0
param([string]$BuildDirectory='build',[string]$TrilogyBuildDirectory='build-trilogy',
      [string]$PythonExe='python',[string]$TestRegex='')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(Get-Process MassEffect1,MassEffect2,MassEffect3,vrserver,vrcompositor -ErrorAction SilentlyContinue){
    throw 'Finish native performance captures before building or running offline tests.'
}
Push-Location $repo
try{
    foreach($directory in $BuildDirectory,$TrilogyBuildDirectory){
        if(-not (Test-Path -LiteralPath (Join-Path $directory 'CMakeCache.txt'))){throw "Configure the build first: $directory"}
    }
    $record=Join-Path $repo ('scratch\camera-source-check-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $record|Out-Null
    Start-Transcript -LiteralPath "$record\transcript.txt"|Out-Null
    try{
        cmake --build $BuildDirectory --config Release --target MEVR-LE1 mevr-tests MEVR-Setup -- /m:2 /nodeReuse:false /p:TrackFileAccess=false
        if($LASTEXITCODE){throw 'LE1/test/setup build failed.'}
        cmake --build $TrilogyBuildDirectory --config Release --target MEVR-LE2 MEVR-LE3 -- /m:2 /nodeReuse:false /p:TrackFileAccess=false
        if($LASTEXITCODE){throw 'LE2/LE3 build failed.'}
        $testArgs=@('--test-dir',$BuildDirectory,'-C','Release','--output-on-failure')
        if($TestRegex){$testArgs+=@('-R',$TestRegex)}
        ctest @testArgs
        if($LASTEXITCODE){throw 'CTest failed.'}
        & "$PSScriptRoot\test-offline.ps1" -PythonExe $PythonExe
        & $PythonExe 'tools/check-spdx.py'
        if($LASTEXITCODE){throw 'SPDX check failed.'}
        "Source checks passed. Transcript: $record"
    }finally{Stop-Transcript|Out-Null}
}finally{Pop-Location}
