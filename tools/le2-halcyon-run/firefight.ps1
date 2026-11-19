# SPDX-License-Identifier: MIT
# ME2's firefight checks (lane CV2, 2026-11-18; research/le2-vrik.md "A gun let go in a fight", research/23 sections 7.6 and 7.7,
# research/le2-traversal.md "The vault Freedom's Progress requires"). With a stretch open (prep.ps1, launch.ps1) and Save 4 loaded
# (load.ps1; Shepard stands at the spaceport with the pistol out), standing hands replayed (tracked controllers: only then do
# the holsters refuse the game's draws):
#   1. the let-go in a fight: the game's combat forced (`set SFXGame bForceCombat true`, BioPawn.InCombat's own flag), the pistol
#      drawn (`mevr weapon pistol`) and let go by the console (`mevr weapon away`, the holsters' own let-go): it must go back
#      (`holsters: the console: the pistol goes back in its holster (in combat, where SFXGameModeDefault.TryHolster refuses: its own
#      last step, BioPawnBehavior.SetActionState(explore))`, then `the pistol is holstered after`);
#   2. a draw nobody asked for: with the weapon away the hand power wheel opened and closed with no pick (`mevr powerwheel open`,
#      `close`: SFXGameModePowerWheel.Deactivated's SetActionState(1)): `came out without a holster grab`, then put back;
#   3. VR cover's two looks at each pawn (`mevr cover pawns` with `judge line`, then `either`): each line names what the eye line
#      and the flat line met;
#   4. the vault holstered and with the pawn turned (vault.ps1 -Holster, -YawDeg 60).
# Then the combat flag back off. Results go to the stretch's session folder, firefight-<time>\ (the console's answers, the mod's
# lines). Not run yet (2026-11-18: the owner was in the headset).
#
#   tools\le2-halcyon-run\firefight.ps1 -Holder <you> [-SkipVault]
param([Parameter(Mandatory)][string]$Holder, [switch]$SkipVault)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('firefight-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE2') 'Logs\LE2MEVR.log'

function Ask([string]$command) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function Tool([string]$name, [string[]]$arguments) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W "tools\$name") @arguments
}
function ReadLog {
    # Shared read: the game keeps the log open for writing.
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return ($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
}
$from = (ReadLog).Count
Ask 'mevr state' | Write-Output

# Standing, tracked hands in view for the whole run (the holsters stand down without tracked controllers).
$stand = Join-Path $out 'stand.mevrrec'
& python (Join-Path $W 'tools\make-walk-recording.py') $stand --plan stand:120 --hands view | Out-Null
Tool 'replay.ps1' @('-Path', $stand, '-Inputs') | Out-Null
Start-Sleep -Seconds 3

try {
    '--- 1. the let-go in a fight'
    Ask 'set SFXGame bForceCombat true' | Write-Output
    Ask 'mevr weapon pistol' | Write-Output
    Start-Sleep -Seconds 4
    Ask 'mevr weapon away' | Write-Output
    Start-Sleep -Seconds 3

    '--- 2. a draw nobody asked for (the hand power wheel closed with no pick)'
    Ask 'set SFXGame bForceCombat false' | Write-Output
    Start-Sleep -Seconds 4  # past kResultSeconds since our own draw
    Ask 'mevr powerwheel open' | Write-Output
    Start-Sleep -Seconds 2
    Ask 'mevr powerwheel close' | Write-Output
    Start-Sleep -Seconds 3

    '--- 3. VR cover: both looks at each pawn'
    Ask 'mevr cover judge line' | Write-Output
    Ask 'mevr cover pawns' | Write-Output
    Ask 'mevr cover judge either' | Write-Output
    Ask 'mevr cover pawns' | Write-Output
    Ask 'mevr cover judge auto' | Write-Output
} finally {
    Ask 'set SFXGame bForceCombat false' | Out-Null
    Tool 'replay.ps1' @('-Stop') | Out-Null
}

if (-not $SkipVault) {
    '--- 4. the vault, holstered and with the pawn turned (Save 4''s CoverLink_10)'
    Tool 'le2-halcyon-run\vault.ps1' @('-Runs', '1', '-Holster') | Write-Output
    Tool 'le2-halcyon-run\vault.ps1' @('-Runs', '1', '-YawDeg', '60') | Write-Output
}

$lines = ReadLog
$new = if ($lines.Count -gt $from) { $lines[$from..($lines.Count - 1)] } else { @() }
$kept = @($new | Where-Object { $_ -match '\] \[\w+\] (holsters:|vr cover:|traversal:|posture: cover watch|power wheel: (opened|closed))' })
$kept | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$kept | Select-Object -First 80
"results: $out"
