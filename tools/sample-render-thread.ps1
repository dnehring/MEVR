# Samples Halcyon's render thread in the running game (LE1): where its time goes, by module and function
# (research/22). Gets the thread from a frame-timing capture of the running process (takes a 2 s one if the
# latest capture is from another run), then runs mevr-stack-sampler (build it: cmake --build build --config
# Release --target mevr-stack-sampler).
#
#   tools/sample-render-thread.ps1               8 s, a sample every millisecond
#   tools/sample-render-thread.ps1 -Seconds 20
param([double]$Seconds = 8, [int]$IntervalUs = 1000)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$sampler = Join-Path $PSScriptRoot '..\build\Release\mevr-stack-sampler.exe'
if (-not (Test-Path $sampler)) { throw "Build it first: cmake --build build --config Release --target mevr-stack-sampler" }
$game = Get-Process MassEffect1 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { throw 'MassEffect1 is not running' }
$dir = Join-Path $MevrDataDir 'perf'
function Latest { Get-ChildItem -LiteralPath $dir -Filter '*-probe.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
$probe = Latest
$meta = if ($probe) { Get-Content -Raw -LiteralPath $probe.FullName | ConvertFrom-Json }
if (-not $meta -or $meta.pid -ne $game.Id) {
    Write-Host 'Taking a 2 s capture to find the render thread...'
    & "$PSScriptRoot\perf-probe.ps1" -Seconds 2 -Tag thread | Out-Null
    $meta = Get-Content -Raw -LiteralPath (Latest).FullName | ConvertFrom-Json
}
Write-Host "Sampling render thread $($meta.renderThread) of pid $($game.Id) for $Seconds s"
& $sampler $game.Id $meta.renderThread $Seconds $IntervalUs
