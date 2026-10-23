# SPDX-License-Identifier: MIT
# Asks the running game (LE1, Halcyon's Stereo 2) for an eye diff and prints it: one paired frame recorded both
# ways (the first eye's D3D11 calls, and the engine's second-eye pass on the shared renderer), compared call by
# call (plugin/DrawReplay, research/32 lever 4). Says whether the two streams align, which constant bytes differ
# between the eyes, and whether the draw replay's substitution covers them.
#
# Writes %LOCALAPPDATA%\MEVR\eyediff.request; the host picks it up within a second, hooks the context if it hasn't
# yet, and reports on the next frame with two compatible scene renders (a VR session in gameplay). The report is
# %LOCALAPPDATA%\MEVR\perf\<stamp>[-tag]-eyediff.txt. Any sharePass value works (0 runs that one frame as 1).
#
#   tools/eye-diff.ps1
#   tools/eye-diff.ps1 -Tag citadel
param(
    [string]$Tag = '',
    [int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$dir = Join-Path $MevrDataDir 'perf'
$since = Get-Date
$text = if ($Tag) { "tag=$Tag" } else { '' }
$request = Write-MevrRequest 'eyediff.request' $text
Write-Host "Requested an eye diff ($request)"

$deadline = $since.AddSeconds($TimeoutSeconds)
$found = $null
while ((Get-Date) -lt $deadline -and -not $found) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $dir) {
        $found = Get-ChildItem -LiteralPath $dir -Filter '*-eyediff.txt' | Where-Object { $_.LastWriteTime -ge $since } |
            Sort-Object LastWriteTime | Select-Object -Last 1
    }
}
if (Test-Path $request) {
    Write-Warning "The game hasn't picked up the request (is it running with the host ASI deployed?)"
}
if (-not $found) {
    Write-Warning "No eye diff within $TimeoutSeconds s (it needs a frame with two compatible scene renders: VR gameplay)"
    exit 1
}
Write-Host $found.FullName
Get-Content -LiteralPath $found.FullName
