# SPDX-License-Identifier: MIT
# ME3's combat fixture (lane AU2, 2026-11-18; research/le3-combat-fixtures.md "A combat fixture by console"): with an ME3
# renderer-preview stretch open and a save loaded (prep.ps1, launch.ps1, load.ps1 with Save 31), the console opens the N7 map
# BioP_SPDish (Firebase Dagger's single-player copy), finalizes its squad selection with the screen's own FinalizeParty (`mevr
# omni party`: Shepard alone), backs out of the weapon loadout (`mevr omni close`), waits out the shuttle's intro (game mode 7,
# then 8) until gameplay (mode 0) and turns on god mode. Shepard then stands on foot as a Soldier with an Avenger assault rifle
# drawn, Cerberus troopers firing at him within a few metres. No save is written or edited; the map's own autosave lands in
# the career's folder, which the stretch's restore puts back. Results in the stretch's session folder, me3-combat-<time>\.
#
#   tools\le2-halcyon-run\me3-combat.ps1 -Holder <you> [-Map BioP_SPDish] [-NoGod] [-Loadout '2 1 all']
#
# -Loadout: before the loadout's B, `mevr loadout <type> [steps] [all]` picks a category's next weapon with the screen's own
# script (type 2: sniper rifles; `all` lists every weapon of the type, not only the career's), so Shepard carries it too: the
# sniper rifle for the scope capture's check (lane AU5).
param(
    [Parameter(Mandatory)][string]$Holder,
    [string]$Map = 'BioP_SPDish',
    [switch]$NoGod,
    [string]$Loadout = ''
)
$ErrorActionPreference = 'Stop'
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le3-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME3 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME3 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect3 -ErrorAction SilentlyContinue)) { throw 'ME3 is not running' }
$out = Join-Path $open.session ('me3-combat-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null

function Ask([string]$command) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
# `mevr state`'s game mode and world, or $null.
function State {
    $s = Ask 'mevr state'
    if ($s -match 'world World (\S+); pawn (\S+) .* game mode (-?\d+)') { return @{ world = $Matches[1]; pawn = $Matches[2]; mode = [int]$Matches[3] } }
    return $null
}
function WaitFor([scriptblock]$done, [int]$seconds, [string]$what) {
    $until = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $until) {
        $s = State
        if ($s -and (& $done $s)) { return $s }
        Start-Sleep -Seconds 3
    }
    throw "timed out after $seconds s waiting for $what (see $out\console.txt)"
}

Ask "open $Map" | Out-Null
$s = WaitFor { param($s) $s.world -like "$Map.*" -and $s.mode -eq 9 } 120 "$Map's squad selection (game mode 9)"
Write-Output ("{0} open: {1}, game mode {2}" -f $Map, $s.pawn, $s.mode)
Start-Sleep -Seconds 3
Write-Output (Ask 'mevr omni party')
Start-Sleep -Seconds 6
if ($Loadout) {
    Write-Output (Ask "mevr loadout $Loadout")
    Start-Sleep -Seconds 2
}
Write-Output (Ask 'mevr omni close')  # the weapon loadout: B keeps the loadout the map gave
$s = WaitFor { param($s) $s.mode -eq 0 } 180 'gameplay after the intro (game mode 0)'
if (-not $NoGod) { Ask 'god' | Out-Null }
$weapon = Ask 'mevr reload status'
Write-Output ("on foot: {0}; {1}" -f $s.pawn, (($weapon -split "`n") | Select-Object -Last 1))
