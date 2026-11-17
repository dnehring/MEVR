# SPDX-License-Identifier: MIT
# The reload by hand's check on the sequels (research/le2-vrik.md "The reload by hand", game/Reload): with a stretch open
# (prep.ps1, launch.ps1) and a save loaded with a gun in Shepard's hand (ME2's Save 4: the pistol out), one replay
# (tools/make-reload-recording.py): the right grip holds the gun and fires once, then the left hand takes a clip at the pouch
# and pushes it into the receiver. The ammo (`mevr reload status`: magazine, used, spare) before, after the shot and after the
# reload; the game's own reload is the only route (no R). Then a second replay without the shot: a full magazine refuses the
# clip. Results go to the stretch's session folder, reload-<time>\ (the recordings, console.txt, the mod's `reload:` lines).
#
#   tools\le2-halcyon-run\reload.ps1 -Holder <you> [-Game LE3] [-Along 0.04] [-Up 0.05]
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [double]$Along = 0.04,
    [double]$Up = 0.05
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$name = if ($Game -eq 'LE3') { 'ME3' } else { 'ME2' }
$current = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\{0}-current.json" -f $Game.ToLowerInvariant())
if (-not (Test-Path -LiteralPath $current)) { throw "no $name stretch is open: prep.ps1, launch.ps1, load.ps1 first" }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open $name stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process ("MassEffect" + $Game.Substring(2)) -ErrorAction SilentlyContinue)) { throw "$name is not running" }
$out = Join-Path $open.session ('reload-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root $Game) ("Logs\{0}MEVR.log" -f $Game)

function Ask([string]$command) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function Tool([string]$tool, [string[]]$arguments) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W "tools\$tool") @arguments
}
function ReadLog {
    # Shared read: the game keeps the log open for writing.
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return ($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
}
$from = (ReadLog).Count
Ask 'mevr state' | Write-Output
# The pistol out first (after holsters.ps1 it may be away): the replay's right grip then holds it.
Ask 'mevr weapon pistol' | Write-Output
Start-Sleep -Seconds 3
"before: " + (Ask 'mevr reload status')

$runs = @(
    @{ name = 'reload'; args = @(); checks = @(@{ at = 2.6; label = 'after the shot' }, @{ at = 9.5; label = 'after the reload' }) },
    @{ name = 'full'; args = @('--no-shot'); checks = @(@{ at = 9.5; label = 'a full magazine' }) },
    # The same shot with the right grip squeezed the whole time ([Holsters] gripHolds' carry): does it still fire?
    @{ name = 'grip-held'; args = @('--grip-held'); checks = @(@{ at = 2.6; label = 'after a shot with the grip held' }) }
)
foreach ($run in $runs) {
    $recording = Join-Path $out ($run.name + '.mevrrec')
    & python (Join-Path $W 'tools\make-reload-recording.py') $recording --along $Along --up $Up @($run.args)
    $started = [Diagnostics.Stopwatch]::StartNew()
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    foreach ($check in $run.checks) {
        $wait = $check.at - $started.Elapsed.TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
        "$($run.name), $($check.label): " + (Ask 'mevr reload status')
        if ($run.name -eq 'reload' -and $check.label -eq 'after the shot') {
            Tool 'snapshot.ps1' @('-Count', '1', '-Delay', '2.0', '-Tag', 'reload-held') | Tee-Object -FilePath (Join-Path $out 'snapshot.txt') | Out-Null
        }
    }
    Tool 'replay.ps1' @('-Stop') | Out-Null
    Start-Sleep -Seconds 2
}

$lines = ReadLog
$new = if ($lines.Count -gt $from) { $lines[$from..($lines.Count - 1)] } else { @() }
$kept = @($new | Where-Object { $_ -match '\] \[\w+\] (reload:|holsters: .*(drawing|holster|in hand)|weapon: .*(in the right hand|not in the right hand))' })
$kept | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$kept | Select-Object -First 60
"results: $out"
