# SPDX-License-Identifier: MIT
# The holsters' check on ME2 (research/le2-vrik.md "Holsters on the sequels"): with a stretch open (prep.ps1, launch.ps1) and
# Save 4 loaded (load.ps1; Shepard stands with the pistol out), replayed right-hand grabs: at the right shoulder (the assault
# rifle: a switch), let go there (the holster); the trigger pulled with the weapon away (refused); at the right hip (the pistol from its holster), let go there; at the hip again,
# let go in front of the chest (it goes back after [Holsters] releaseGrace). A snapshot while the pistol is held. Results go
# to the stretch's session folder, holsters-<time>\ (the recordings, the snapshot's name, the mod's `holsters:` lines).
#
#   tools\le2-halcyon-run\holsters.ps1 -Holder <you>
param([Parameter(Mandatory)][string]$Holder)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('holsters-' + (Get-Date -Format 'HHmmss'))
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

$runs = @(
    @{ name = 'shoulder'; args = @('--slot', 'shoulder'); seconds = 14; snapshotAt = 0 },
    @{ name = 'hip'; args = @('--slot', 'hip'); seconds = 14; snapshotAt = 7 },
    @{ name = 'hip-away'; args = @('--slot', 'hip', '--away'); seconds = 14; snapshotAt = 0 }
)
foreach ($run in $runs) {
    $recording = Join-Path $out ($run.name + '.mevrrec')
    & python (Join-Path $W 'tools\make-holster-recording.py') $recording @($run.args)
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    if ($run.snapshotAt -gt 0) {
        Start-Sleep -Seconds $run.snapshotAt
        Tool 'snapshot.ps1' @('-Count', '1', '-Tag', ('holsters-' + $run.name)) | Tee-Object -FilePath (Join-Path $out ('snapshot-' + $run.name + '.txt'))
        Start-Sleep -Seconds ([Math]::Max(1, $run.seconds - $run.snapshotAt - 3))
    } else {
        Start-Sleep -Seconds $run.seconds
    }
    Tool 'replay.ps1' @('-Stop') | Out-Null
    Start-Sleep -Seconds 2
    if ($run.name -eq 'shoulder') {
        # The weapon is away now: the trigger must not draw it (only holsters draw; `holsters: the game wanted to draw
        # your weapon (Function SFXGame.SFXGameModeBase.FireWeapon): refused`).
        $pull = Join-Path $out 'trigger.mevrrec'
        & python (Join-Path $W 'tools\make-ads-recording.py') $pull 8 90 1.75 0.085 1.5 0.1 --no-support --no-grip
        Tool 'replay.ps1' @('-Path', $pull, '-Inputs') | Out-Null
        Start-Sleep -Seconds 9
        Tool 'replay.ps1' @('-Stop') | Out-Null
        Start-Sleep -Seconds 2
    }
}

$lines = ReadLog
$new = if ($lines.Count -gt $from) { $lines[$from..($lines.Count - 1)] } else { @() }
$kept = @($new | Where-Object { $_ -match '\] \[\w+\] (holsters:|weapon: .*(in the right hand|not in the right hand))' })
$kept | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$kept | Select-Object -First 60
"results: $out"
