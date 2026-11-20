# SPDX-License-Identifier: MIT
# People leaving a first-person conversation on ME2 and ME3 (game/ConversationExit, research/conversation-exits.md section 9),
# by script on the fake headset: with a stretch open (prep.ps1, launch.ps1, load.ps1) the world mode on for this session, a
# conversation started, ended early by the world's own call after -Talk seconds (so the script's removals at its end run), and
# the eyes snapshotted while the people it removes walk away; the module's lines collected, the settings given back.
#
#   tools\le2-halcyon-run\conversation-exit.ps1 -Holder <you> [-Game LE2|LE3] -Start fire:<Class>:<text> [-Talk 12]
#       [-After 16] [-Every 2] [-Exit walk|hold|off] [-Before 'console line', ...] [-Tag what]
#
# -Start fire:<Class>:<t>  `mevr instances <Class> <t>`, then `mevr activate 0 0`: the level's own action fired (a start action,
#                          or the first action of the chain that leads to it).
# -Talk <s>                seconds in the conversation before `mevr convo exit end` (0: let it play; -After then waits for it).
# -Exit                    `mevr convo exit <word>` for the run (walk: the default; off: the broken run, the game's own pop).
# -Before                  console lines run before the start (`mevr setloc ...`, `god`).
# Results: the stretch's session folder, exit-<tag>-<time>\ (steer.txt, console.txt, snapshots.txt, lines.txt: the module's
# `convo exit:` and `ai walk:` lines, the audit's, the sequels' `conversation world:` lines and the renderer's [VRSTATE]).
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [Parameter(Mandatory)][string]$Start,
    [double]$Talk = 12,
    [double]$After = 16,
    [double]$Every = 2,
    [ValidateSet('walk', 'hold', 'off')][string]$Exit = 'walk',
    [string[]]$Before = @(),
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'exit'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
. "$PSScriptRoot\..\steer-common.ps1"
$name = if ($Game -eq 'LE3') { 'ME3' } else { 'ME2' }
$current = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\{0}-current.json" -f $Game.ToLowerInvariant())
if (-not (Test-Path -LiteralPath $current)) { throw "no $name stretch is open: prep.ps1, launch.ps1, load.ps1 first" }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open $name stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process ('MassEffect' + $Game.Substring(2)) -ErrorAction SilentlyContinue)) { throw "$name is not running" }
if ($Start -notmatch '^fire:([A-Za-z0-9_.]+):(.*)$') { throw '-Start: fire:<Class>:<text>' }
$fireClass = $Matches[1]
$fireText = $Matches[2]
$out = Join-Path $open.session ('exit-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
Steer-Init -Out $out
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$log = Join-Path $bin ('Logs\{0}MEVR.log' -f $Game)
$halcyonLog = Join-Path $env:LOCALAPPDATA ('{0}\{0}_Log.txt' -f $(if ($Game -eq 'LE3') { 'MELE3VR' } else { 'MELE2VR' }))
function ReadLines([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    $s = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite,Delete')
    try { return @((New-Object IO.StreamReader($s)).ReadToEnd() -split "`r?`n") } finally { $s.Dispose() }
}
$from = (ReadLines $log).Count
$fromHalcyon = (ReadLines $halcyonLog).Count
$started = Get-Date
Steer-Note ("conversation exit on {0}: start {1}, talk {2} s, worldExit {3}" -f $name, $Start, $Talk, $Exit)
Steer-Note ('world: ' + (Steer-Ask 'mevr convo world on').Trim())
Steer-Note ('exit: ' + (Steer-Ask "mevr convo exit $Exit").Trim())
Steer-Note ('shots: ' + (Steer-Ask 'mevr convo exit shots on').Trim())
Steer-Hold
try {
    foreach ($line in $Before) { Steer-Note ("before: {0}: {1}" -f $line, (Steer-Ask $line).Trim()); Start-Sleep -Milliseconds 1500 }
    Steer-Note ('instances: ' + (Steer-Ask ("mevr instances {0} {1}" -f $fireClass, $fireText)).Trim())
    Steer-Note ('activate: ' + (Steer-Ask 'mevr activate 0 0').Trim())
    $deadline = (Get-Date).AddSeconds($Talk)
    while ((Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; Steer-Note ('state: ' + (Steer-Ask 'mevr state').Trim()) }
    if ($Talk -gt 0) {
        Steer-Snapshot "$Tag-before-end"
        Steer-Note ('end: ' + (Steer-Ask 'mevr convo exit end').Trim())
    }
    $deadline = (Get-Date).AddSeconds($After)
    $n = 0
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds ([int]($Every * 1000))
        ++$n
        Steer-Note ('status: ' + (Steer-Ask 'mevr convo exit status').Trim())
        Steer-Snapshot ("$Tag-$n")
    }
} finally {
    Steer-Stop
    Steer-Note ('shots back: ' + (Steer-Ask 'mevr convo exit shots off').Trim())
    Steer-Note ('exit back: ' + (Steer-Ask 'mevr convo exit auto').Trim())
    Steer-Note ('world back: ' + (Steer-Ask 'mevr convo world auto').Trim())
    $lines = @(ReadLines $log | Select-Object -Skip $from | Select-String 'convo exit|ai walk:|convo: audit|conversation world:|exit hooks|hooked AActor') |
        ForEach-Object { $_.Line }
    $lines += '--- renderer'
    $lines += @(ReadLines $halcyonLog | Select-Object -Skip $fromHalcyon | Select-String '\[VRSTATE\]|transition fade') | ForEach-Object { $_.Line }
    Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Value $lines -Encoding utf8
    Steer-Snapshots -Since $started | Out-Null
    Steer-Note ("lines: {0} -> {1}" -f $lines.Count, (Join-Path $out 'lines.txt'))
}
