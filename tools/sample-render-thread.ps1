# SPDX-License-Identifier: MIT
# Samples Halcyon's render thread in the running game (LE1, or a sequel with -Game: research/sequel-render-levers.md):
# where its time goes, by module and function
# (research/22). Gets the thread from a frame-timing capture of the running process (takes a 2 s one if the
# latest capture is from another run), then runs mevr-stack-sampler (build it: cmake --build build --config
# Release --target mevr-stack-sampler).
#
# The raw stacks go to %LOCALAPPDATA%\MEVR\perf\<stamp>-stacks.txt, grouped by the game's functions with
# tools/stack-functions.py (-Under RVA drills into one function's callees).
#
#   tools/sample-render-thread.ps1               8 s, a sample every millisecond
#   tools/sample-render-thread.ps1 -Seconds 20 -Under 0x76EE20
#   tools/sample-render-thread.ps1 -Game LE2 -Top 40          ME2's render thread, its 40 heaviest functions
param([double]$Seconds = 8, [int]$IntervalUs = 1000, [string]$Under = '',
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1', [int]$Top = 30)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$sampler = @('build', 'build-trilogy') | ForEach-Object { Join-Path $PSScriptRoot "..\$_\Release\mevr-stack-sampler.exe" } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sampler) { throw "Build it first: cmake --build build --config Release --target mevr-stack-sampler (or in build-trilogy)" }
$processName = "MassEffect$($Game.Substring(2))"
$game = Get-Process $processName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $game) { throw "$processName is not running" }
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
$raw = Join-Path $dir ("{0:yyyy-MM-dd_HH-mm-ss}-stacks.txt" -f (Get-Date))
& $sampler $game.Id $meta.renderThread $Seconds $IntervalUs $raw
$extra = @('--game', $Game, '--top', $Top)
if ($Under) { $extra += @('--under', $Under) }
& python -I "$PSScriptRoot\stack-functions.py" $raw @extra
