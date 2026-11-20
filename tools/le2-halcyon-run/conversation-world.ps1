# SPDX-License-Identifier: MIT
# First-person conversations in the world on ME2 and ME3 (docs/trilogy-plan.md T49, game/SequelConversationWorld; research/
# le2-conversations-in-world.md), by script on the fake headset: with a stretch open (prep.ps1, launch.ps1, load.ps1) the world
# mode set for this session (`mevr convo world on|off`), a conversation started, snapshots of the eyes while it plays, the
# module's and the renderer's lines collected, the setting given back.
#
#   tools\le2-halcyon-run\conversation-world.ps1 -Holder <you> [-Game LE2|LE3] [-World on|off] [-Start a|fire:<Class>:<text>]
#       [-FaceYaw <deg>] [-Seconds 40] [-Every 6] [-Walk 0] [-Tag what]
#
# -Start a                 the tracked hands stand still and A is pressed (the game's use of what the head looks at: the
#                          mod's interact line names it first); -FaceYaw turns the view there before (Steer-Face).
# -Start fire:<Class>:<t>  `mevr instances <Class> <t>`, then `mevr activate 0 0`: the level's own start action fired
#                          (SFXSeqAct_StartConversation / BioSeqAct_StartConversation, as ME1's conversation-launch fires it).
# -Walk <s>                during the conversation the left stick pushed forward that long (a player walking, never an
#                          automatic walk), once the first snapshot is taken.
# Results: the stretch's session folder, convo-<tag>-<time>\ (steer.txt, console.txt, snapshots.txt, lines.txt: the mod's
# `conversation world:` lines and the renderer's [VRSTATE] lines since the start).
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [ValidateSet('on', 'off')][string]$World = 'on',
    [string]$Start = 'a',
    [double]$FaceYaw = [double]::NaN,
    [double]$Seconds = 40,
    [double]$Every = 6,
    [double]$Walk = 0,
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'convo'
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
$out = Join-Path $open.session ('convo-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
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

Steer-Note ("conversation world on {0}: world {1}, start {2}" -f $name, $World, $Start)
Steer-Note ('state: ' + (Steer-Ask 'mevr state').Trim())
Steer-Note ('world: ' + (Steer-Ask "mevr convo world $World").Trim())
Steer-Hold
Start-Sleep -Seconds 1
try {
    if (-not [double]::IsNaN($FaceYaw)) { Steer-Face -Yaw $FaceYaw -NoCheck; Start-Sleep -Milliseconds 800 }
    if ($Start -eq 'a') {
        $selected = @(ReadLines $log | Select-Object -Skip $from | Select-String 'interact: (A would use|nothing to use)' | Select-Object -Last 1 |
            ForEach-Object { $_.Line })
        Steer-Note ('interact: ' + ($selected -join ''))
        Steer-Play 'stand+a:0.15,stand:0.5'
    } elseif ($Start -match '^fire:([A-Za-z0-9_.]+):(.*)$') {
        Steer-Note ('instances: ' + (Steer-Ask ("mevr instances {0} {1}" -f $Matches[1], $Matches[2])).Trim())
        Steer-Note ('activate: ' + (Steer-Ask 'mevr activate 0 0').Trim())
    } else {
        throw "-Start: a or fire:<Class>:<text>"
    }
    $deadline = (Get-Date).AddSeconds($Seconds)
    $n = 0
    $walked = $Walk -le 0
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds ([int]($Every * 1000))
        ++$n
        Steer-Note ('state: ' + (Steer-Ask 'mevr state').Trim() + ' | ' + (Steer-Ask 'mevr convo world').Trim())
        Steer-Snapshot ("$Tag-$n")
        if (-not $walked) {
            $walked = $true
            $null = Steer-Push -Angle 0 -Seconds $Walk
        }
    }
} finally {
    Steer-Stop
    Steer-Note ('world back: ' + (Steer-Ask 'mevr convo world auto').Trim())
    $lines = @(ReadLines $log | Select-Object -Skip $from | Select-String 'conversation world:|halcyon bridge: (a conversation|the conversation)|depth of field|letterbox') |
        ForEach-Object { $_.Line }
    $lines += '--- renderer'
    $lines += @(ReadLines $halcyonLog | Select-Object -Skip $fromHalcyon | Select-String '\[VRSTATE\]|\[MEVR-HELDVIEW\]|CAPTURE_STALL') | ForEach-Object { $_.Line }
    Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Value $lines -Encoding utf8
    Steer-Snapshots -Since $started | Out-Null
    Steer-Note ("lines: {0} -> {1}" -f $lines.Count, (Join-Path $out 'lines.txt'))
}
