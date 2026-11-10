# SPDX-License-Identifier: MIT
# The conversation sweep's runner (LE1, research/69 "The sweep: plan"): plays the work list of tools/conversation-sweep.py
# (`plan scan.jsonl --out plan.json`) in a running game, unattended, and writes one result per scene for the judge
# (`conversation-sweep.py judge`).
#
#   tools/conversation-sweep.ps1 -Plan plan.json -ReachOnly          the reach matrix: open each level, stream each chunk,
#                                                                    look for each start action; fire nothing
#   tools/conversation-sweep.ps1 -Plan plan.json                     the pass: every scene played to its end
#   tools/conversation-sweep.ps1 -Plan plan.json -Resume             go on where the last run in the same folder stopped
#   tools/conversation-sweep.ps1 -Plan plan.json -Maps BIOA_STA00 -Only sta20_chorban,sta70_garrus
#   tools/conversation-sweep.ps1 -Plan plan.json -Out <folder> -Seed 2    another choice of replies (the branch pass)
#
# Per level: `disconnect`, `open <level>`, wait for gameplay (a level waiting in the party screen is sent on with the squad
# screen's own FinalizeParty; a conversation that starts by itself is played and recorded as "(by itself)"). Per chunk: the
# loaded streaming volume whose plain state shows it is touched (`mevr stream touch`), or its named streaming state set
# first (`mevr stream state <name> on`), then the wait until `mevr stream chunks` says loaded and visible. Per scene:
# Shepard 1.5 m in front of the owner facing it (else on the stage), the start action fired (`mevr activate <n> 0`), the
# conversation driven to its end (a reply picked while the wheel is up, in an order seeded by the conversation's name and
# -Seed; the line skipped otherwise), a flowed scene waited out by its `convo flow: summary` line and its follow-up driven
# too. The record takes the mod's `convo: summary` and `convo flow: summary` lines from the scene's stretch of the log, which
# is also saved whole (logs\<conversation>.log) for whoever reads the exceptions.
#
# A watchdog: when the game's process is gone (a crash) or its console stops answering (a hang: the process is ended), the
# scene is recorded so, the game started again (tools/launch-game.ps1 -Steam), the level and chunk brought back and the run
# goes on with the next scene. Results: <Out>\results.jsonl, reach.jsonl, runner.log; Out defaults to
# %LOCALAPPDATA%\MEVR\sweep\<date>. Game data: nothing of it goes into the repository.
#
# Needs the game folder (tools/game-slot.ps1 take ... -Set 'Cinematics.conversations=world'), the fake headset and the game
# running (tools/launch-game.ps1 -Steam). It opens levels: the slot's backup covers the saves. Never `streamlevelin`.
param(
    [Parameter(Mandatory = $true)] [string]$Plan,
    [string]$Out = '',
    [string[]]$Maps = @(),
    [string[]]$Only = @(),
    [switch]$ReachOnly,
    [switch]$Resume,
    [int]$Seed = 1,
    # How lines are paced. skip: every line is skipped about two seconds in (the smoke pass: fast, but what a line does
    # after its start, an authored walk, a camera move, a late facing key, is cut short). shepard: only Shepard's own lines
    # are skipped. full: nothing is skipped, every line plays its length, as a player who listens hears it.
    [ValidateSet('skip', 'shepard', 'full')] [string]$Pace = 'skip',
    [int]$SceneSeconds = 0,
    [int]$LoadSeconds = 240,
    [int]$StreamSeconds = 45,
    [double]$StandM = 1.5,
    [int]$Limit = 0,
    [switch]$NoRestart
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
if ($SceneSeconds -le 0) { $SceneSeconds = $(if ($Pace -eq 'skip') { 150 } elseif ($Pace -eq 'shepard') { 420 } else { 720 }) }
$win64 = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1\Binaries\Win64'
$log = Join-Path $win64 'Logs\LE1MEVR.log'
if (-not $Out) { $Out = Join-Path $env:LOCALAPPDATA ("MEVR\sweep\{0}" -f (Get-Date -Format 'yyyy-MM-dd')) }
New-Item -ItemType Directory -Force (Join-Path $Out 'logs') | Out-Null
$resultsFile = Join-Path $Out $(if ($ReachOnly) { 'reach-scenes.jsonl' } else { 'results.jsonl' })
$reachFile = Join-Path $Out 'reach.jsonl'
$runnerLog = Join-Path $Out 'runner.log'
$doc = Get-Content -LiteralPath $Plan -Raw | ConvertFrom-Json

# A line added to a file someone may be reading right now (a `tail` of the runner's log ended the first run, 2026-11-09:
# Add-Content wants the file to itself): opened for append with readers and writers let in, tried again for a few seconds.
function AddLine([string]$file, [string]$line) {
    for ($try = 0; $try -lt 40; $try++) {
        try {
            $fs = [IO.File]::Open($file, 'Append', 'Write', 'ReadWrite')
            try { $bytes = [Text.Encoding]::UTF8.GetBytes($line + "`r`n"); $fs.Write($bytes, 0, $bytes.Length) } finally { $fs.Close() }
            return
        } catch { Start-Sleep -Milliseconds 250 }
    }
    Write-Warning "could not write to $file in 10 s: $line"
}
function Say([string]$text) {
    $line = "{0} {1}" -f (Get-Date -Format 'HH:mm:ss'), $text
    Write-Host $line
    AddLine $runnerLog $line
}
function Append([string]$file, $record) { AddLine $file ($record | ConvertTo-Json -Compress -Depth 8) }

$script:silent = 0
function Console([string]$line, [int]$timeout = 10) {
    $r = ((& (Join-Path $tools 'console.ps1') $line -TimeoutSeconds $timeout 2>$null 3>$null) | Out-String).Trim()
    if ($r) { $script:silent = 0 } else { $script:silent++ }
    return $r
}
function Running() { [bool](Get-Process MassEffect1 -ErrorAction SilentlyContinue) }
function State() {
    $r = Console 'mevr convo'
    $m = [regex]::Match($r, 'game mode (-?\d+)')
    $c = [regex]::Match($r, 'conversation (\d+) \(([^)]*)\)')
    $e = [regex]::Match($r, 'entry (-?\d+), wheel (up|down)')
    [pscustomobject]@{
        answered = [bool]$r; mode = $(if ($m.Success) { [int]$m.Groups[1].Value } else { -2 }); convo = $c.Success
        name = $(if ($c.Success) { $c.Groups[2].Value } else { '' }); entry = $(if ($e.Success) { [int]$e.Groups[1].Value } else { -1 })
        wheel = ($e.Success -and $e.Groups[2].Value -eq 'up'); text = $r
        shepardSpeaks = [bool]([regex]::Match($r, 'speaker SFXPawn_Player').Success)
    }
}

# The mod's log from a position on (the file is shared with the game; a log that shrank started anew).
function LogMark() { if (Test-Path -LiteralPath $log) { (Get-Item -LiteralPath $log).Length } else { 0 } }
function LogSince([long]$from) {
    if (-not (Test-Path -LiteralPath $log)) { return @() }
    $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try {
        if ($from -gt $fs.Length) { $from = 0 }
        [void]$fs.Seek($from, 'Begin')
        $reader = New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8)
        return @($reader.ReadToEnd() -split "`r?`n")
    } finally { $fs.Close() }
}
function Summaries([string[]]$lines, [string]$pattern) {
    $out = @()
    foreach ($line in $lines) {
        $m = [regex]::Match($line, $pattern)
        if ($m.Success) { try { $out += ($m.Groups[1].Value | ConvertFrom-Json) } catch { } }
    }
    return $out
}
function Key([string]$name) {
    # 'BioConversation sta20_chorban_D.sta20_chorban_dlg' -> 'sta20_chorban'
    $tail = ($name -split ' ')[-1]
    return (($tail -split '\.')[0] -replace '_[Dd]$', '').ToLower()
}

# A stable seed per conversation and -Seed, so a re-run takes the same replies.
function SceneSeed([string]$name) { $h = $Seed * 7919; foreach ($ch in $name.ToCharArray()) { $h = ($h * 31 + [int]$ch) % 1000003 }; return [int]$h }

# Drives the running conversation to its end: a used wheel slot picked while the wheel is up, the line skipped otherwise.
function Drive([string]$name, [datetime]$deadline, $walk = $null) {
    $rng = New-Object System.Random (SceneSeed $name)
    $picks = 0; $skips = 0; $quiet = 0; $forced = $false
    $began = Get-Date
    $walks = @($walk | Where-Object { $null -ne $_ })
    while ($quiet -lt 2) {
        $s = State
        if (-not $s.answered) { if (-not (Running) -or $script:silent -ge 3) { break }; Start-Sleep -Seconds 1; continue }
        if (-not $s.convo) { $quiet++; Start-Sleep -Milliseconds 700; continue }
        $quiet = 0
        if ((Get-Date) -gt $deadline) { $forced = $true; Console 'mevr convo end' | Out-Null; Start-Sleep -Seconds 3; break }
        # A scene's `walks` (each x, y, seconds; the canary set): the stick without controllers, one after the other from 6 s
        # in, each while the wheel is down (the wheel takes the stick); the lines play on by themselves meanwhile.
        if ($walks.Count -gt 0 -and -not $s.wheel -and ((Get-Date) - $began).TotalSeconds -ge 6) {
            $w = $walks[0]; $walks = @($walks | Select-Object -Skip 1)
            Say ('   walk: ' + ((Console ("mevr convo walk {0} {1} {2}" -f $w[0], $w[1], $w[2])) -replace '^.*convo: ', ''))
            Start-Sleep -Seconds ([int][double]$w[2] + 1)
            continue
        }
        if ($s.wheel) {
            foreach ($slot in (0..5 | Sort-Object { $rng.Next() })) {
                $r = Console "mevr convo pick $slot"
                if ($r -match 'called true') { $picks++; break }
                if ($r -match "the wheel isn't up|no conversation") { break }
            }
        } elseif ($Pace -eq 'skip' -or ($Pace -eq 'shepard' -and $s.shepardSpeaks)) {
            $r = Console 'mevr convo skip'
            if ($r -match 'called true') { $skips++ }
        }
        Start-Sleep -Milliseconds 900
    }
    return [pscustomobject]@{ picks = $picks; skips = $skips; forced = $forced }
}

# Plays whatever conversation is running now to its end and records it (a scene that started by itself).
function PlayByItself([string]$map) {
    $mark = [math]::Max(0, (LogMark) - 200000)
    $s = State
    $k = Key $s.name
    $planned = $script:sceneByName[$k]
    $drive = Drive $s.name ((Get-Date).AddSeconds([math]::Max($SceneSeconds, 300))) $(if ($planned) { $planned.walks } else { $null })
    Start-Sleep -Seconds 1
    $window = @(LogSince $mark)
    $sum = @(Summaries $window 'convo: summary (\{.*\})') | Select-Object -Last 1
    if ($sum) { $k = Key $sum.name }
    Set-Content -LiteralPath (Join-Path $Out "logs\$k.log") -Value $window -Encoding utf8
    $script:done[$k] = $true  # the plan's own scene of it is not fired again
    Append $resultsFile ([ordered]@{ conversation = $k; map = $map; action = '(by itself)'; route = 'open'; outcome = 'played'
        ended = $(if ($drive.forced) { 'forced' } else { 'itself' }); summary = $sum; driver = [ordered]@{ picks = $drive.picks; skips = $drive.skips } })
    Say ("   (by itself) {0}: {1}" -f $k, $(if ($drive.forced) { 'forced end' } else { 'played' }))
    $script:byItself += $k
}

# Waits for gameplay (mode 0 three polls in a row); the party screen is sent on; a conversation that starts is played.
function Settle([string]$map, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $steady = 0; $guiSince = $null
    while ((Get-Date) -lt $deadline -and $steady -lt 3) {
        $s = State
        if (-not $s.answered) { if (-not (Running)) { return $false }; Start-Sleep -Seconds 2; continue }
        if ($s.convo) {
            # A scene that starts by itself is played out; the wait for gameplay begins anew after it (the wake-up scene at
            # full length outlasted the level's whole waiting time, and the level was taken for one that never opened).
            if ($ReachOnly) { Console 'mevr convo end' | Out-Null; Start-Sleep -Seconds 3 } else { PlayByItself $map; $deadline = (Get-Date).AddSeconds([math]::Max($seconds, 60)) }
            $steady = 0; continue
        }
        if ($s.mode -eq 0) { $steady++ } else { $steady = 0 }
        if ($s.mode -eq 7) {
            if ($null -eq $guiSince) { $guiSince = Get-Date }
            elseif (((Get-Date) - $guiSince).TotalSeconds -gt 20) {
                $panels = Console 'mevr panel'
                if ($panels -match 'GameOver') { return $false }
                Console 'mevr vehicle party' | Out-Null; $guiSince = $null; Start-Sleep -Seconds 5
            }
        } else { $guiSince = $null }
        Start-Sleep -Seconds 2
    }
    return $steady -ge 3
}

function Open([string]$map) {
    Console 'disconnect' | Out-Null
    Start-Sleep -Seconds 15
    Console "open $map" | Out-Null
    Start-Sleep -Seconds 5
    $ok = Settle $map $LoadSeconds
    if ($ok) { Start-Sleep -Seconds 2; Console 'god' | Out-Null }
    return $ok
}
# Shepard is there (a pawn the console can ask about). A volume touched from afar streams out the floor he stands on: he
# falls to the kill plane and the level has no player (`mevr stream list` then answers "(no pawn)": the first reach run lost
# most of the Citadel and Noveria to it, 2026-11-09). So he floats while chunks stream (the `ghost` cheat: flying, no
# collision) and walks again once he stands by the scene's owner (`walk`).
function PawnOk() { $r = Console 'mevr loc'; return ([bool]$r -and $r -notmatch 'no pawn') }

# The game back after a crash or a hang.
function Restart() {
    if ($NoRestart) { throw 'the game is gone and -NoRestart was given' }
    $p = Get-Process MassEffect1 -ErrorAction SilentlyContinue
    if ($p) { Say '   the console is silent: ending the game process'; $p | Stop-Process -Force -Confirm:$false; Start-Sleep -Seconds 8 }
    Say '   starting the game again'
    $script:restarts++
    try { & (Join-Path $tools 'launch-game.ps1') -Steam 2>&1 | Out-Null } catch { Say "   launch-game: $_" }
    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline -and -not (State).answered) { Start-Sleep -Seconds 5 }
    $script:silent = 0
    return (State).answered
}
function Dead() { return (-not (Running)) -or $script:silent -ge 3 }

function ChunkOf([string]$package) {
    # BIOA_STA60_05A_DSG -> sta60_05a (a state's chunk names are spelled so, with or without the bioa_)
    return (($package -replace '^(?i)BIOA_', '') -replace '(?i)_(DSG|LAY|ART|SND|PLC|CIN|DS\d).*$', '').ToLower()
}
function VisibleChunks() {
    $set = @{}
    foreach ($line in ((Console 'mevr stream chunks') -split "`n")) {
        $m = [regex]::Match($line, '^\s*(\S+) loaded=(\d) visible=(\d)')
        if ($m.Success -and $m.Groups[2].Value -eq '1' -and $m.Groups[3].Value -eq '1') { $set[(ChunkOf $m.Groups[1].Value)] = $true; $set[$m.Groups[1].Value.ToLower()] = $true }
    }
    return $set
}
# The loaded streaming volumes: index (for `mevr stream touch`, valid until the next change), and per state its name and the
# chunks it shows.
function LoadedVolumes() {
    $list = @(); $current = $null
    foreach ($line in ((Console 'mevr stream list') -split "`n")) {
        $m = [regex]::Match($line, '^\[(\d+)\] (\S+) at ')
        if ($m.Success) { $current = [pscustomobject]@{ index = [int]$m.Groups[1].Value; name = $m.Groups[2].Value; states = @() }; $list += $current; continue }
        if ($null -eq $current) { continue }
        $s = [regex]::Match($line, '^\s*state (\S+) \(in [^)]*\): visible ([^;]*);')
        if ($s.Success) { $current.states += [pscustomobject]@{ state = $s.Groups[1].Value; shows = @(($s.Groups[2].Value -split ',') | ForEach-Object { ($_ -replace '^(?i)bioa_', '').ToLower() } | Where-Object { $_ }) } }
    }
    return $list
}

# Streams a chunk in: `ready`, the route taken, the seconds it took. A chunk deep in a hub is reached in hops (the plan's
# `streamVia`: the chunks on the way, from the scan's volumes): at each step the farthest chunk of the way that a loaded
# volume shows is streamed in (a plain state by a touch; a named one with its streaming state set first), which loads the
# volumes for the next. Volumes are touched by their index in the listing just made: their names repeat across packages.
function Stream($scene) {
    $t0 = Get-Date
    $tail = ($scene.actionTail -split '\.')[-1]
    $before = Console ("mevr instances BioSeqAct_StartConversation " + $tail)
    if ($before -match [regex]::Escape($scene.actionTail)) { return [pscustomobject]@{ ready = $true; route = 'loaded'; seconds = 0 } }
    $target = (ChunkOf $scene.package)
    $way = @(@($scene.streamVia) | Where-Object { $_ } | ForEach-Object { $_.ToLower() })
    if ($way.Count -eq 0 -or $way[-1] -ne $target) { $way += $target }
    $route = @()
    $ready = $false
    for ($step = 0; $step -lt ($way.Count + 2) -and -not $ready; $step++) {
        if ((VisibleChunks).ContainsKey($target)) { $ready = $true; break }
        $volumes = LoadedVolumes
        $hit = $null
        for ($i = $way.Count - 1; $i -ge 0 -and $null -eq $hit; $i--) {
            foreach ($plain in $true, $false) {
                foreach ($v in $volumes) {
                    foreach ($st in $v.states) {
                        $isPlain = ($st.state -eq 'None' -or $st.state -eq 'Default')
                        if ($isPlain -eq $plain -and $st.shows -contains $way[$i]) { $hit = [pscustomobject]@{ hop = $way[$i]; volume = $v; state = $(if ($isPlain) { $null } else { $st.state }) }; break }
                    }
                    if ($hit) { break }
                }
                if ($hit) { break }
            }
        }
        if ($null -eq $hit) {
            # No loaded volume shows any chunk of the way: the catalogue's named state alone, once.
            if ($step -eq 0 -and $scene.streamStates -and @($scene.streamStates).Count -gt 0) {
                $name = @($scene.streamStates)[0]
                Console "mevr stream state $name on" | Out-Null
                $route += "state $name (no volume)"
                Start-Sleep -Seconds 4
                continue
            }
            break
        }
        if ($hit.state) { Console "mevr stream state $($hit.state) on" | Out-Null; Start-Sleep -Seconds 1 }
        Console ("mevr stream touch " + $hit.volume.index) | Out-Null
        $route += $(if ($hit.state) { "$($hit.hop) by state $($hit.state) and $($hit.volume.name)" } else { "$($hit.hop) by $($hit.volume.name)" })
        $deadline = (Get-Date).AddSeconds($StreamSeconds)
        $arrived = $false
        while ((Get-Date) -lt $deadline -and -not $arrived) {
            Start-Sleep -Seconds 2
            $arrived = (VisibleChunks).ContainsKey($hit.hop)
            if (Dead) { break }
        }
        if (-not $arrived) { $route += "($($hit.hop) never became visible)"; break }
        if ($hit.hop -eq $target) { $ready = $true }
        Start-Sleep -Seconds 1
    }
    if ($ready) { Start-Sleep -Seconds 2 }
    return [pscustomobject]@{ ready = $ready; route = $(if ($route.Count) { $route -join '; ' } else { 'none' }); seconds = [int]((Get-Date) - $t0).TotalSeconds }
}

function Place($scene) {
    $x = $null
    if ($scene.ownerAt) {
        $yaw = [double]($(if ($null -ne $scene.ownerYaw) { $scene.ownerYaw } else { 0 }))
        $rad = $yaw * [math]::PI / 180
        $x = [double]$scene.ownerAt[0] + [math]::Cos($rad) * $StandM * 100; $y = [double]$scene.ownerAt[1] + [math]::Sin($rad) * $StandM * 100
        $z = [double]$scene.ownerAt[2]; $face = ((($yaw + 180) % 360) + 360) % 360
    } elseif ($scene.stageAt) { $x = [double]$scene.stageAt[0]; $y = [double]$scene.stageAt[1]; $z = [double]$scene.stageAt[2] + 90; $face = $null }
    if ($null -eq $x) { return $false }
    Console ("mevr setloc {0:0} {1:0} {2:0}" -f $x, $y, ($z + 50)) | Out-Null
    if ($null -ne $face) {
        $unreal = [int][math]::Round($face * 65536 / 360) % 65536
        Console ("set SFXPawn_Player Rotation (Pitch=0,Yaw={0},Roll=0)" -f $unreal) | Out-Null
        Console ("set BioPlayerController Rotation (Pitch=0,Yaw={0},Roll=0)" -f $unreal) | Out-Null
    }
    return $true
}

function FindAction($scene) {
    $found = Console ("mevr instances BioSeqAct_StartConversation " + ($scene.actionTail -split '\.')[-1])
    foreach ($line in ($found -split "`n")) {
        $m = [regex]::Match($line, '^\[(\d+)\] BioSeqAct_StartConversation (\S+)')
        if ($m.Success -and $m.Groups[2].Value -like ("*" + $scene.actionTail)) { return [int]$m.Groups[1].Value }
    }
    return $null
}

function RunScene([string]$map, $scene, $streamed) {
    $record = [ordered]@{
        conversation = $scene.conversation; map = $map; chunk = $scene.chunk; action = $scene.actionTail; route = 'open'; seed = $Seed; pace = $Pace
        expect = [ordered]@{ verdict = $scene.verdict; flowed = [bool]$scene.flowed }; stream = $streamed.route
    }
    if (-not $streamed.ready) { $record.outcome = 'not reached'; $record.detail = "the chunk never became loaded and visible ($($streamed.route))"; return $record }
    $index = FindAction $scene
    if ($null -eq $index) { $record.outcome = 'action not loaded'; $record.detail = 'the chunk is visible, the start action is not among the loaded ones'; return $record }
    if ($ReachOnly) { $record.outcome = 'reached'; return $record }
    $placed = Place $scene
    Console 'walk' | Out-Null
    Start-Sleep -Seconds 2
    $s = State
    if ($s.convo) { PlayByItself $map; $s = State }
    if ($s.mode -ne 0) { if (-not (Settle $map 30)) { $record.outcome = 'not reached'; $record.detail = "not in gameplay before firing (game mode $($s.mode))"; return $record } }
    $index = FindAction $scene
    if ($null -eq $index) { $record.outcome = 'action not loaded'; $record.detail = 'gone after Shepard was placed'; return $record }
    $mark = LogMark
    $t0 = Get-Date
    Console ("mevr activate $index 0") | Out-Null
    # A scene's `alsoFire` (the canary set: a bark and another scene's start over a flowed scene): fired half a second later.
    if ($scene.alsoFire) {
        Start-Sleep -Milliseconds 500
        foreach ($tail in @($scene.alsoFire)) {
            $other = Console ("mevr instances BioSeqAct_StartConversation " + ($tail -split '\.')[-1])
            $om = [regex]::Match($other, '(?m)^\[(\d+)\] BioSeqAct_StartConversation \S*' + [regex]::Escape($tail))
            if ($om.Success) { Console ("mevr activate $($om.Groups[1].Value) 0") | Out-Null; Say "   also fired $tail" } else { Say "   alsoFire: $tail is not loaded" }
        }
    }
    $deadline = $t0.AddSeconds($SceneSeconds)
    # Up to 12 s for a conversation, or for the flow module's line that a chain of ours plays as barks.
    $started = $false; $barks = $false
    $wait = $t0.AddSeconds(12)
    while ((Get-Date) -lt $wait -and -not $started) {
        Start-Sleep -Seconds 1
        if ((State).convo) { $started = $true; break }
        if (@(LogSince $mark) -match 'convo flow: .* plays as barks') { $started = $true; $barks = $true }
        if (Dead) { break }
    }
    $drive = [pscustomobject]@{ picks = 0; skips = 0; forced = $false }
    if ($barks) {
        # The chain plays in game mode 0: wait for its summary, then for the level's follow-up (a conversation), and drive that.
        while ((Get-Date) -lt $deadline -and -not (@(LogSince $mark) -match 'convo flow: summary') -and -not (Dead)) {
            if ((State).convo) { break }
            Start-Sleep -Seconds 2
        }
        $follow = (Get-Date).AddSeconds(6)
        while ((Get-Date) -lt $follow -and -not (State).convo) { Start-Sleep -Seconds 1 }
    }
    if ((State).convo) { $drive = Drive $scene.conversation $deadline $scene.walks }
    Start-Sleep -Seconds 2
    $window = @(LogSince $mark)
    Set-Content -LiteralPath (Join-Path $Out "logs\$($scene.conversation).log") -Value $window -Encoding utf8
    $sums = @(Summaries $window 'convo: summary (\{.*\})')
    $flows = @(Summaries $window 'convo flow: summary (\{.*\})')
    $mine = @($sums | Where-Object { (Key $_.name) -eq $scene.conversation }) | Select-Object -Last 1
    if (-not $mine) { $mine = $sums | Select-Object -Last 1 }
    $record.seconds = [int]((Get-Date) - $t0).TotalSeconds
    $record.placed = $placed
    $record.ended = $(if ($drive.forced) { 'forced' } else { 'itself' })
    $record.driver = [ordered]@{ picks = $drive.picks; skips = $drive.skips }
    if ($mine) { $record.summary = $mine }
    if ($sums.Count -gt 1) { $record.summaries = @($sums | ForEach-Object { [ordered]@{ name = (Key $_.name); why = $_.why; seconds = $_.seconds; lines = $_.lines; world = $_.world } }) }
    $flow = @($flows | Where-Object { $_.name -eq $scene.conversation }) | Select-Object -Last 1
    if ($flow) { $record.flow = $flow }
    $record.notes = @($window | Select-String -Pattern 'convo flow: .*(held|refused|fired again|let through)|convo: .*(no place for the player|goes to the 3D screen|set piece)' | ForEach-Object { $_.Line.Substring([math]::Min(15, $_.Line.Length)) } | Select-Object -First 12)
    if (Dead) { $record.outcome = $(if (Running) { 'hang' } else { 'crash' }); $record.detail = 'during the scene' }
    elseif ($mine -or $flow) { $record.outcome = 'played' }
    else { $record.outcome = 'no conversation' }
    return $record
}

# ---- the run ----
$script:done = @{}
if ($Resume -and (Test-Path -LiteralPath $resultsFile)) {
    foreach ($line in Get-Content -LiteralPath $resultsFile) { if ($line.Trim()) { $script:done[(($line | ConvertFrom-Json).conversation)] = $true } }
}
$done = $script:done
$script:sceneByName = @{}
foreach ($level in $doc.maps) { foreach ($chunk in $level.chunks) { foreach ($scene in $chunk.scenes) { $script:sceneByName[$scene.conversation] = $scene } } }
$script:restarts = 0
$script:byItself = @()
$total = 0
Say ("the sweep: {0} scenes in the plan; {1}; out {2}" -f $doc.scenes, $(if ($ReachOnly) { 'reach only' } else { "seed $Seed, pace $Pace, $SceneSeconds s a scene at most" }), $Out)
if (-not (State).answered) { if (-not (Restart)) { throw 'the game does not answer its console (start it: tools/launch-game.ps1 -Steam)' } }
foreach ($level in $doc.maps) {
    $map = $level.map
    if ($Maps.Count -gt 0 -and $Maps -notcontains $map) { continue }
    $scenes = @($level.chunks | ForEach-Object { $_.scenes } | Where-Object { ($Only.Count -eq 0 -or $Only -contains $_.conversation) -and -not $done.ContainsKey($_.conversation) })
    if ($scenes.Count -eq 0) { continue }
    Say "== $map ($($scenes.Count) scenes)"
    $t0 = Get-Date
    $opened = Open $map
    if (-not $opened -and (Dead)) { if (Restart) { $opened = Open $map } }
    $volumes = if ($opened) { @((Console 'mevr stream list') -split "`n" | Where-Object { $_ -match '^\[\d+\] ' }).Count } else { 0 }
    Append $reachFile ([ordered]@{ kind = 'level'; map = $map; opened = $opened; seconds = [int]((Get-Date) - $t0).TotalSeconds; mode = (State).mode; volumes = $volumes; byItself = @($script:byItself) })
    $script:byItself = @()
    if (-not $opened) {
        Say "   $map never reached gameplay"
        foreach ($scene in @($scenes | Where-Object { -not $done.ContainsKey($_.conversation) })) { Append $resultsFile ([ordered]@{ conversation = $scene.conversation; map = $map; chunk = $scene.chunk; action = $scene.actionTail; route = 'open'; outcome = 'not reached'; detail = "open $map never reached gameplay"; expect = [ordered]@{ verdict = $scene.verdict; flowed = [bool]$scene.flowed } }) }
        continue
    }
    foreach ($chunk in $level.chunks) {
        $mine = @($chunk.scenes | Where-Object { ($Only.Count -eq 0 -or $Only -contains $_.conversation) -and -not $done.ContainsKey($_.conversation) })
        if ($mine.Count -eq 0) { continue }
        $streamed = $null
        foreach ($scene in $mine) {
            if ($Limit -gt 0 -and $total -ge $Limit) { break }
            if ($done.ContainsKey($scene.conversation)) { continue }  # played by itself meanwhile
            $total++
            if (Dead) {
                if (-not (Restart)) { throw 'the game did not come back' }
                if (-not (Open $map)) { Say "   $map never reached gameplay after the restart"; break }
                $streamed = $null
            }
            if (-not $ReachOnly -and -not (Settle $map 40)) {
                Say "   not in gameplay before $($scene.conversation): the level again"
                if (-not (Open $map)) { if ((Dead) -and (Restart)) { [void](Open $map) } }
                $streamed = $null
            }
            if ($null -eq $streamed -or -not $streamed.ready -or $null -eq (FindAction $scene)) {
                if (-not (PawnOk)) {
                    Say "   no pawn before $($scene.conversation) (Shepard fell or died): the level again"
                    if (-not (Open $map)) { if ((Dead) -and (Restart)) { [void](Open $map) } }
                }
                Console 'ghost' | Out-Null
                $streamed = Stream $scene
                if ($null -eq (FindAction $scene) -and $streamed.ready -and -not $ReachOnly) {
                    # Gone with a scene's follow-up (it left the level or streamed the chunk out): the level again, once.
                    if (Open $map) { $streamed = Stream $scene }
                }
                Append $reachFile ([ordered]@{ kind = 'chunk'; map = $map; chunk = $chunk.chunk; ready = $streamed.ready; route = $streamed.route; seconds = $streamed.seconds })
            }
            $record = RunScene $map $scene $streamed
            Append $resultsFile $record
            Say ("   {0,-34} {1,-18} {2}" -f $scene.conversation, $record.outcome, $(if ($record.summary) { "{0} lines, kept {1}, withheld {2}{3}" -f $record.summary.lines, $record.summary.kept, $record.summary.sourceKept, $(if ($record.flow) { ', flowed' } else { '' }) } elseif ($record.flow) { 'flowed' } else { [string]$record.detail }))
        }
    }
}
Say ("done: {0} scenes, {1} restarts; {2}" -f $total, $script:restarts, $resultsFile)
