# SPDX-License-Identifier: MIT
# Says whether the running Mass Effect is drawing its world, and fails fast when a load doesn't finish.
#
# Born of the 2026-11-19/20 ME3 hangs (docs/knowledge/gotchas/me2-scale-set-during-load.md): the game sat on its loading
# picture for minutes, still answering the console, while the lane kept polling and the owner noticed first. The host
# writes a "world" block into %LOCALAPPDATA%\MEVR\testloop.status once a second (src/plugin/TestLoop.cpp WorldJson, with
# render_levers::WorldDrawn): when it was written, and how long since a Present and since a Present with a scene render.
# tools/common.ps1 Get-MevrWorldVerdict turns it into one state: no-game, starting, drawn, no-world (a loading screen or a
# movie), blocked (the game thread stopped writing: a hard map load or a hang), no-frames (nothing presented).
#
#   tools/game-watch.ps1                         one verdict now; exit 0 drawn, 1 starting or loading, 2 stuck, 4 no game
#   tools/game-watch.ps1 -UntilWorld             after an action that can load (a pick, a travel, a save's load): waits
#                                                until the world is drawn again (exit 0), or fails at -TimeoutS (exit 2)
#                                                or when the game goes away (exit 4), with evidence
#   tools/game-watch.ps1 -Watch                  runs until stopped: one line per change of state, and a STUCK line
#                                                (with evidence) once no-world, blocked or no-frames outlasts -LimitS;
#                                                for a background watcher (the coordinator's Monitor) or a terminal
#   tools/game-watch.ps1 -Watch -AlertsOnly      only the STUCK lines and GONE (a game that went away in the middle of
#                                                a load, or with a crash report): one notification per failure
#
# Evidence on a failure, in %LOCALAPPDATA%\MEVR\game-watch\<stamp>-<game>-<state>\: testloop.status, the tail of the
# game's mod log and of Halcyon's log, any crash report newer than the watch's start, and a snapshot of the eyes when the
# game thread still answers. %LOCALAPPDATA%\MEVR\game-watch.alert (JSON) holds the last failure.
#
# Normal loads on this machine take 10-40 s; a stuck one never ends. -LimitS / -TimeoutS (default 120) is the line.
param(
    [switch]$UntilWorld,
    [switch]$Watch,
    [double]$TimeoutS = 120,
    [double]$LimitS = 120,
    [double]$IntervalS = 2,
    [switch]$NoEvidence,
    [switch]$Quiet,
    # -Watch: print only STUCK lines and a game's unexpected exit (a monitor's notifications), not every change of state
    [switch]$AlertsOnly
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$started = Get-Date

function Out-Line([string]$text) {
    Write-Output ("[{0}] game-watch: {1}" -f (Get-Date -Format 'HH:mm:ss'), $text)
}

function Save-Evidence($v, [string]$why) {
    if ($NoEvidence) { return $null }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $dir = Join-Path $MevrDataDir ("game-watch\{0}-{1}-{2}" -f $stamp, $(if ($v.Game) { $v.Game } else { 'none' }), $v.State)
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $status = Join-Path $MevrDataDir 'testloop.status'
    if (Test-Path $status) { Copy-Item -LiteralPath $status -Destination $dir }
    try {
        if ($v.Game) {
            $bin = Get-MELEBinDir (Find-MELEInstall).Root $v.Game
            $log = Join-Path $bin "Logs\$($v.Game)MEVR.log"
            if (Test-Path $log) { Get-Content -LiteralPath $log -Tail 400 | Set-Content -LiteralPath (Join-Path $dir "$($v.Game)MEVR-tail.log") -Encoding utf8 }
        }
    } catch { }
    foreach ($h in @("$env:LOCALAPPDATA\MELEVR\MELEVR_Log.txt", "$env:LOCALAPPDATA\MELE2VR\MELE2VR_Log.txt", "$env:LOCALAPPDATA\MELE3VR\MELE3VR_Log.txt")) {
        if ((Test-Path $h) -and (Get-Item $h).LastWriteTime -gt $started.AddMinutes(-30)) {
            Get-Content -LiteralPath $h -Tail 200 | Set-Content -LiteralPath (Join-Path $dir ((Split-Path $h -Leaf) -replace '\.txt$', '-tail.txt')) -Encoding utf8
        }
    }
    foreach ($c in 'crash-breadcrumbs.txt', 'crash-firstchance.txt', 'crash-exit.txt') {
        $p = Join-Path $MevrDataDir $c
        if ((Test-Path $p) -and (Get-Item $p).LastWriteTime -gt $started) { Copy-Item -LiteralPath $p -Destination $dir }
    }
    if ($v.State -eq 'no-world') {
        # The game thread still ticks: ask for what the eyes show (the host polls the request once a second).
        $asked = Get-Date
        Write-MevrRequest 'snapshot.request' 'tag=game-watch' | Out-Null
        Start-Sleep -Seconds 4
        Get-ChildItem (Join-Path $MevrDataDir 'snapshots') -Filter '*-left.png' -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -gt $asked } | Sort-Object LastWriteTime -Descending | Select-Object -First 1 |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dir }
    }
    $alert = [ordered]@{ when = (Get-Date).ToString('o'); why = $why; state = $v.State; seconds = [int]$v.Seconds;
        game = $v.Game; pid = $v.Pid; detail = $v.Detail; evidence = $dir }
    ($alert | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $MevrDataDir 'game-watch.alert') -Encoding utf8
    ($alert | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $dir 'verdict.json') -Encoding utf8
    return $dir
}

function Test-Bad($v, [double]$limit) {
    return ($v.State -in 'no-world', 'blocked', 'no-frames') -and $v.Seconds -ge $limit
}

if ($UntilWorld) {
    # Waits through a load: the world drawn for two readings in a row ends it.
    $deadline = $started.AddSeconds($TimeoutS)
    $drawn = 0
    $last = ''
    $sawGame = $false
    while ($true) {
        $v = Get-MevrWorldVerdict
        if ($v.State -ne 'no-game') { $sawGame = $true }
        if (-not $Quiet -and $v.State -ne $last) { Out-Line $v.Detail; $last = $v.State }
        if ($v.State -eq 'drawn') {
            if (++$drawn -ge 2) { Out-Line "the world is drawn after $([int]((Get-Date) - $started).TotalSeconds) s"; exit 0 }
        } else {
            $drawn = 0
        }
        if ($v.State -eq 'no-game' -and $sawGame) {
            $dir = Save-Evidence $v 'the game went away while waiting for its world'
            Out-Line "FAILED: the game went away while waiting for its world$(if ($dir) { "; evidence: $dir" })"
            exit 4
        }
        if ((Get-Date) -gt $deadline) {
            $dir = Save-Evidence $v "no world drawn within $TimeoutS s"
            Out-Line "STUCK: no world drawn within $TimeoutS s: $($v.Detail)$(if ($dir) { "; evidence: $dir" })"
            exit 2
        }
        Start-Sleep -Seconds $IntervalS
    }
}

if ($Watch) {
    # One line per change of state; a STUCK line (and evidence) once per stuck spell.
    $last = ''
    $reported = $false
    $lastGame = $null
    if (-not $AlertsOnly) { Out-Line "watching (stuck after $LimitS s without a drawn world)" }
    while ($true) {
        $v = Get-MevrWorldVerdict
        $key = "$($v.Game)/$($v.Pid)/$($v.State)"
        if ($key -ne $last) {
            # Waiting states are logged once they last a few seconds, so a flicker between drawn and no-world stays quiet.
            if ($v.State -in 'drawn', 'no-game', 'starting' -or $v.Seconds -ge 10) {
                if (-not $AlertsOnly) { Out-Line $v.Detail }
                $last = $key
                $reported = $false
            }
        }
        # A game that goes away without its run lock released and with a crash report newer than it, or in the middle of a
        # no-world spell, is worth a line even in -AlertsOnly (the 2026-11-20 ME3 that vanished 59 s into a load).
        if ($v.State -eq 'no-game' -and $null -ne $lastGame) {
            $crash = @('crash-breadcrumbs.txt', 'crash-firstchance.txt', 'crash-exit.txt') | ForEach-Object { Join-Path $MevrDataDir $_ } |
                Where-Object { (Test-Path $_) -and (Get-Item $_).LastWriteTime -gt $lastGame.Seen.AddMinutes(-1) }
            if ($crash -or $lastGame.State -in 'no-world', 'blocked', 'no-frames') {
                Out-Line ("GONE: {0} (pid {1}) went away while {2}{3}" -f $lastGame.Game, $lastGame.Pid, $lastGame.State,
                    $(if ($crash) { "; crash report: $(@($crash) -join ', ')" } else { '' }))
            }
            $lastGame = $null
        } elseif ($v.State -ne 'no-game') {
            # A quit stops the game thread a moment before the process ends: blocked counts only once it lasted 15 s.
            $state = if ($v.State -eq 'blocked' -and $v.Seconds -lt 15) { 'quitting?' } else { $v.State }
            $lastGame = [pscustomobject]@{ Game = $v.Game; Pid = $v.Pid; State = $state; Seen = Get-Date }
        }
        if (-not $reported -and (Test-Bad $v $LimitS)) {
            $dir = Save-Evidence $v "$($v.State) for $([int]$v.Seconds) s"
            Out-Line "STUCK: $($v.Detail)$(if ($dir) { "; evidence: $dir" })"
            $reported = $true
        }
        Start-Sleep -Seconds $IntervalS
    }
}

$v = Get-MevrWorldVerdict
if (-not $Quiet) { Out-Line $v.Detail }
if ($v.State -eq 'no-game') { exit 4 }
if (Test-Bad $v $LimitS) {
    $dir = Save-Evidence $v "$($v.State) for $([int]$v.Seconds) s"
    if ($dir) { Out-Line "STUCK; evidence: $dir" }
    exit 2
}
if ($v.State -eq 'drawn') { exit 0 }
exit 1
