# SPDX-License-Identifier: MIT
# Puts the running game into a drivable Mako without a save that has one (research/47 "Reaching the Mako").
#
# Loads an uncharted world (BIOA_UNC20 by default) through the console, waits for its squad screen (game mode 7),
# accepts it through the screen's own handler (`mevr vehicle party`: FinalizeParty as the confirmation's A), and waits
# for the level's own rover drop to make the Mako the pawn (game mode 1). The level's script does the rest: the squad
# screen's end streams in its cinematic layer, the drop plays, Shepard enters the Mako. Needs the mod loaded and a
# local player (any level, flat or in VR).
#
#   tools/mako-start.ps1                  BIOA_UNC20
#   tools/mako-start.ps1 -Map BIOA_UNC21  another uncharted world
param(
    [string]$Map = 'BIOA_UNC20',
    [int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'

function Invoke-Console([string]$line) {
    # console.ps1 exits 1 on a timeout and sets no exit code otherwise: its output is the answer.
    $out = & "$PSScriptRoot\console.ps1" $line 2>$null
    return ($out -join "`n")
}

function Wait-Mode([string]$mode, [datetime]$deadline) {
    while ((Get-Date) -lt $deadline) {
        $state = Invoke-Console 'mevr vehicle'
        if ($state -match "map $Map, game mode $mode,") { return $state }
        Start-Sleep -Seconds 2
    }
    return $null
}

$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
Write-Host "Loading $Map ..."
Invoke-Console "open $Map" | Out-Null
if (-not (Wait-Mode '7' $deadline)) {
    Write-Warning "No squad screen in $Map within $TimeoutSeconds s (``mevr vehicle`` says what the game is doing)"
    exit 1
}
Write-Host 'Squad screen up; accepting it.'
Write-Host (Invoke-Console 'mevr vehicle party')
$state = Wait-Mode '1' $deadline
if (-not $state) {
    Write-Warning "The Mako didn't become the pawn within $TimeoutSeconds s (``mevr vehicle levels`` shows the streaming)"
    exit 1
}
Write-Host $state
