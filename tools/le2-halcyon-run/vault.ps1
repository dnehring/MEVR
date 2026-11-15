# SPDX-License-Identifier: MIT
# The vault's fixture on ME2 (research/le2-traversal.md "ME2: the first gate"): Freedom's Progress's spaceport, CoverLink_10's slot
# that mantles over the low wall to CoverLink_9. With a stretch open (prep.ps1, launch.ps1) and Save 4 loaded (load.ps1), each run
# stands Shepard in front of the slot (mevr traversal place: a test placement), lets the world settle, and commits one vault: the
# double A replayed through the right controller (-Mode tap, tools/make-vault-recording.py), or the console's own commit (-Mode go,
# no controllers). It takes eye snapshots through the move (-Snapshots) and collects the mod's `traversal:` lines of each run.
#
#   tools\le2-halcyon-run\vault.ps1 [-Runs 3] [-Mode tap|go|single] [-Link CoverLink_10] [-Slot 0] [-BackM 0.25] [-View game|level]
#       [-Mask none|vignette|blink] [-Snapshots] [-Grip]
#
# The first A must not snap Shepard into cover: that is ME2's VR cover block (game/Posture, `[Hands] noCover = 1`, the default;
# research/le2-vrik-audit.md), checked here with `mevr cover snap` before the runs. Results go to the stretch's session folder,
# vault-<stamp>\ (the log lines, the console's answers, the snapshots' names).
param(
    [int]$Runs = 3,
    [ValidateSet('tap', 'go', 'single')][string]$Mode = 'tap',
    [string]$Link = 'CoverLink_10',
    [int]$Slot = 0,
    [double]$BackM = 0.25,
    [string]$View = '',
    [string]$Mask = '',
    [switch]$Snapshots,
    [switch]$Grip,
    [double]$SettleSeconds = 2.0
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$session = (Get-Content -LiteralPath $current -Raw | ConvertFrom-Json).session
$out = Join-Path $session ('vault-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE2') 'Logs\LE2MEVR.log'
$console = Join-Path $W 'tools\console.ps1'

function Ask([string]$command, [int]$timeout = 10) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File $console $command -TimeoutSeconds $timeout 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}

function ReadLog {
    # Shared read: the game keeps the log open for writing.
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return ($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
}

function LogLines([int]$from) {
    $lines = ReadLog
    if ($lines.Count -le $from) { return @() }
    return $lines[$from..($lines.Count - 1)]
}

$recording = Join-Path $out 'vault.mevrrec'
$recArgs = @($recording, '--start', '1.5', '--hands', 'view')
if ($Mode -eq 'single') { $recArgs += '--single' }
if ($Grip) { $recArgs += '--grip' }
& python (Join-Path $W 'tools\make-vault-recording.py') @recArgs | Tee-Object -FilePath (Join-Path $out 'recording.txt')

Ask 'mevr cover snap auto' | Write-Output  # the VR cover block as [Hands] noCover says (game/Posture; 1, the default: refused)
if ($View) { Ask "mevr traversal view $View" | Out-Null }
if ($Mask) { Ask "mevr traversal mask $Mask" | Out-Null }
Ask 'mevr traversal slots' | Write-Output

for ($run = 1; $run -le $Runs; $run++) {
    $from = (ReadLog).Count
    "--- run $run ($Mode)"
    Ask "mevr traversal place $Link $Slot $BackM" | Write-Output
    Start-Sleep -Seconds $SettleSeconds
    Ask 'mevr traversal near' | Write-Output
    if ($Mode -eq 'go') {
        Ask 'mevr traversal go' | Write-Output
        if ($Snapshots) {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\snapshot.ps1') -Count 6 -Interval 0.25 -Tag "vault-run$run" | Out-Null
        }
        Start-Sleep -Seconds 4
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Path $recording -Inputs | Out-Null
        if ($Snapshots) {
            # The double tap is 1.5 s into the replay: the first snapshot just before it (the cue), then the move's end (a snapshot of
            # both eyes takes about 4 s to write; for the middle of a move use -Mode go with `mevr traversal pause <s>`).
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\snapshot.ps1') -Count 2 -Interval 0.2 -Delay 1.0 -Tag "vault-run$run" | Out-Null
            Start-Sleep -Seconds 2
        } else {
            Start-Sleep -Seconds 6
        }
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\replay.ps1') -Stop | Out-Null
    }
    Start-Sleep -Seconds 1
    $lines = @(LogLines $from | Where-Object { $_ -match 'traversal' })
    $lines | Set-Content -LiteralPath (Join-Path $out "run$run.txt") -Encoding utf8
    $lines
}
"results: $out"
