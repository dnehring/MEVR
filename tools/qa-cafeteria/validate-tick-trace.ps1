# SPDX-License-Identifier: MIT
# Build and check the buffered hook/queue timings without changing the installed game.
param([string]$BuildDirectory='build',[string]$TrilogyBuildDirectory='build-trilogy',[switch]$AllowFunctionalRoute)
$ErrorActionPreference='Stop'
if(-not $AllowFunctionalRoute -and (Get-Process MassEffect1,vrserver,vrcompositor -ErrorAction SilentlyContinue)){
    throw 'Coordinate native ownership first. Only a functional route (never a clean capture) can overlap this build.'
}
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $repo ('scratch\tick-trace-check-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $out|Out-Null
Start-Transcript -LiteralPath "$out\transcript.txt"|Out-Null
Push-Location $repo
try{
    cmake --build $BuildDirectory --config Release --target MEVR-LE1 mevr-tests -- /m:2 /nodeReuse:false /p:TrackFileAccess=false
    if($LASTEXITCODE){throw 'Tick-trace build failed.'}
    cmake --build $TrilogyBuildDirectory --config Release --target MEVR-LE2 MEVR-LE3 -- /m:2 /nodeReuse:false /p:TrackFileAccess=false
    if($LASTEXITCODE){throw 'LE2/LE3 build failed.'}
    ctest --test-dir $BuildDirectory -C Release -R '^(mevr-tests|mevr-perf-probe)$' --output-on-failure
    if($LASTEXITCODE){throw 'Tick-trace tests failed.'}
    "Tick-trace checks passed. Transcript: $out"
}finally{Pop-Location;Stop-Transcript|Out-Null}
