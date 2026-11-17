# SPDX-License-Identifier: MIT
# The render thread's slow frames, by function (research/sequel-render-levers.md "Performance research"): a frame
# capture (tools/perf-probe.ps1's request) and render-thread stack samples (mevr-stack-sampler, with its per-sample
# clock) taken over the same seconds, then tools/perf-spike-stacks.py: each game function's time per frame in the slow
# frames against the rest. Any of the three games, in gameplay; replay a head recording first if the view should move.
#
#   tools/perf-spike-stacks.ps1 -Game LE2 -Seconds 15
#   tools/perf-spike-stacks.ps1 -Game LE3 -Seconds 15 -Percentile 90
param([ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1', [double]$Seconds = 15, [int]$IntervalUs = 1000,
    [double]$Threshold = 0, [double]$Percentile = 0, [int]$Top = 30, [string]$Tag = 'spikes')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$sampler = @('build', 'build-trilogy') | ForEach-Object { Join-Path $PSScriptRoot "..\$_\Release\mevr-stack-sampler.exe" } |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sampler) { throw "Build it first: cmake --build build-trilogy --config Release --target mevr-stack-sampler" }
$process = Get-Process "MassEffect$($Game.Substring(2))" -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw "the game ($Game) is not running" }
$dir = Join-Path $MevrDataDir 'perf'
function Latest { Get-ChildItem -LiteralPath $dir -Filter '*-probe.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
$meta = if (Latest) { Get-Content -Raw -LiteralPath (Latest).FullName | ConvertFrom-Json }
if (-not $meta -or $meta.pid -ne $process.Id) {
    & "$PSScriptRoot\perf-probe.ps1" -Seconds 2 -Tag thread | Out-Null
    $meta = Get-Content -Raw -LiteralPath (Latest).FullName | ConvertFrom-Json
}
$stamp = "{0:yyyy-MM-dd_HH-mm-ss}" -f (Get-Date)
$raw = Join-Path $dir "$stamp-$Tag-stacks.txt"
$times = Join-Path $dir "$stamp-$Tag-sample-times.csv"
$since = Get-Date
[void](Write-MevrRequest 'perf.request' "seconds=$Seconds delay=0 tag=$Tag")
# The probe starts within a second of the request; the sampler covers it from now and a second past its end.
& $sampler $process.Id $meta.renderThread ($Seconds + 2.5) $IntervalUs $raw $times | Out-Null
$probe = $null
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline -and -not $probe) {
    Start-Sleep -Milliseconds 500
    $probe = Get-ChildItem -LiteralPath $dir -Filter "*-$Tag-probe.json" | Where-Object { $_.LastWriteTime -ge $since } | Select-Object -Last 1
}
if (-not $probe) { throw "no capture within 30 s" }
$extra = @('--game', $Game, '--top', $Top, '--interval-us', $IntervalUs)
if ($Threshold) { $extra += @('--threshold', $Threshold) }
if ($Percentile) { $extra += @('--percentile', $Percentile) }
& python -I "$PSScriptRoot\perf-spike-stacks.py" $probe.FullName $raw $times @extra
