# SPDX-License-Identifier: MIT
# Runs console commands in the running game, as its own console would, and prints what they print.
#
# Writes %LOCALAPPDATA%\MEVR\console.request (one command per line); the mod (game/ConsoleRequest) polls it twice a
# second, runs each through the local player's PlayerController.ConsoleCommand and writes console.result. Needs a
# local player (the main menu has one too); hot-reloaded with the mod, flat or in VR. For A/B tests without a restart
# (research/22), e.g.
#
#   tools/console.ps1 UMBRA                         lists Umbra's switches and their state
#   tools/console.ps1 "UMBRA OCCLUSIONCULLING"      toggles Umbra's occlusion culling
#   tools/console.ps1 "stat fps" "TOGGLEOCCLUSION"  several commands, in order
param(
    [Parameter(Mandatory = $true, Position = 0, ValueFromRemainingArguments = $true)][string[]]$Command,
    [int]$TimeoutSeconds = 10
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$result = Join-Path $MevrDataDir 'console.result'
if (Test-Path $result) { Remove-Item -LiteralPath $result }
$request = Write-MevrRequest 'console.request' ($Command -join "`n")
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
while ((Get-Date) -lt $deadline -and -not (Test-Path $result)) {
    Start-Sleep -Milliseconds 200
}
if (-not (Test-Path $result)) {
    Write-Warning "No result within $TimeoutSeconds s (is the game running, with a player, and the mod loaded?)"
    if (Test-Path $request) { Remove-Item -LiteralPath $request }
    exit 1
}
Get-Content -Raw -LiteralPath $result
