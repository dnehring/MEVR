# Asks the running game (LE1, Halcyon's renderer) for a frame-breakdown capture and summarises it.
#
# Writes %LOCALAPPDATA%\MEVR\perf.request; the host polls it once a second and, from the next frame,
# records for -Seconds where each frame's time goes (plugin/PerfProbe, research/22). In
# %LOCALAPPDATA%\MEVR\perf:
#   <stamp>[-tag]-frames.csv   per frame of the render thread: each step's time and busy time, draws,
#                              GPU timestamps on the CPU's clock, access violations
#   <stamp>[-tag]-ticks.csv    per game-thread tick: the engine's tick and ours, wall and busy
#   <stamp>[-tag]-probe.json   the clocks, Halcyon's settings, the resolution (written last)
# Then runs tools/perf-probe.py on it: the breakdown and which of render thread, GPU or game thread
# sets the pace. The capture's first and last frames include a GPU drain (the clock sync).
#
#   tools/perf-probe.ps1                       20 s from now
#   tools/perf-probe.ps1 -Seconds 30 -Delay 5  30 s, starting in 5 s
#   tools/perf-probe.ps1 -Tag res60            "-res60" in the file names (an A/B, say)
param(
    [double]$Seconds = 20,
    [double]$Delay = 0,
    [string]$Tag = '',
    [int]$TimeoutSeconds = 0
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$dir = Join-Path $MevrDataDir 'perf'
$since = Get-Date
$text = "seconds=$Seconds delay=$Delay"
if ($Tag) { $text += " tag=$Tag" }
$request = Write-MevrRequest 'perf.request' $text
Write-Host "Requested: $text ($request)"

if ($TimeoutSeconds -le 0) { $TimeoutSeconds = [int](20 + $Delay + $Seconds) }
$deadline = $since.AddSeconds($TimeoutSeconds)
$found = $null
while ((Get-Date) -lt $deadline -and -not $found) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $dir) {
        $found = Get-ChildItem -LiteralPath $dir -Filter '*-probe.json' | Where-Object { $_.LastWriteTime -ge $since } |
            Sort-Object LastWriteTime | Select-Object -Last 1
    }
}
if (Test-Path $request) {
    Write-Warning "The game hasn't picked up the request (is it running with the host ASI deployed?)"
}
if (-not $found) {
    Write-Warning "No capture within $TimeoutSeconds s"
    exit 1
}
Write-Host $found.FullName
& python "$PSScriptRoot\perf-probe.py" $found.FullName
