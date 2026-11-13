# SPDX-License-Identifier: MIT
# Scenes played from validation saves, with the plot each save set (research/69 "From a save"): the fast loop for the
# first-person scenes. Loads a save in the running game through the loaded career's quicksave slot (no `open`, which resets
# the plot the save set, and no restart), fires the scene's first action as the player's use or walk-in would, drives what
# follows (a line skipped a couple of seconds in, the first reply picked) until nothing plays any more, and has
# tools/conversation-session.py judge that stretch of the log. About a minute and a half a scene; ten scenes in one pass.
#
#   tools/conversation-from-save.ps1 -Save John02_81 -Action PlayerBecomesSpectre.BioSeqAct_PMCheckState_2
#       [-Class BioSeqAct_PMCheckState] [-ActionInput 0] [-Seconds 300] [-EndAfter 0] [-Pace skip|full] [-Snapshots 20,45]
#       [-Walk 18] [-Replay rec.mevrrec] [-Before "mevr calltrace 20 hidden"] [-NoLoad] [-Stances] [-Chunk STA30_02A]
#       [-Probe '3:mevr stream chunks','7:mevr instances BioPawn STA70']
#   tools/conversation-from-save.ps1 -All -Career John02 [-FirstSlot 80] [-List tools/conversation-headset-sample.json]
#       [-Plans a.json,b.json] [-Only 80,81] [-EndAfter 25] [-Seconds 150] [-SaveDir <folder of the list's saves>]
#
# -EndAfter N: a conversation is ended N seconds in (only the opening matters). -Walk N: the left stick held forward for N
# seconds from the start (a player who walks along; `mevr convo walk`). -WanderAt N: N seconds into a conversation (the wheel
# down) the stick sideways for 3 s, then standing (the conversational circle gathers round again). -TurnAt N: N seconds into a
# conversation (the wheel down) the player turned as by the right stick (`mevr convo turn`): 90 degrees, 90 more, then back,
# 2 s apart, a stance after each (with -Stances; the early end waits for them). -Lines N (with -Stances): a stance at each of a
# conversation's first N lines (whom each head turns to as the lines change), the early end only after them. -Stances: the scene from above
# (research/69 "Stances from above"; `mevr convo stances on`): after its opening, each walk and its end, drawn by
# tools/conversation-stance.py into a sheet per conversation; a scene where nothing will move (`mevr convo ahead`: nobody walking,
# nobody of the dialogue who would walk in, and the scan knows no stage walk in it) is ended once its stance is written.
# -Probe 'seconds:console line': the line run that many seconds after the scene is fired and its output printed (the runner
# owns console.request, so a probe from another script would race it).
# -Replay: a recording replayed in a loop with its
# inputs from the moment the scene is fired until it is over (a real stick and buttons: tools/make-walk-recording.py). -All: every scene of the list from its numbered
# save (the saves `conversation-sweep.py saves` wrote), each one's action from the plans (`conversation-sweep.py actions`).
#
# The game must be running with a career loaded (any). The save's bytes go over that career's own quicksave (the game
# writes one first, else QuickLoad refuses the file): back up the Save folder before (tools/game-slot.ps1 does) and put the
# career's quicksave back after. -NoLoad fires in the game as it stands.
param(
    [string]$Save = '',
    [string]$Action = '',
    [string]$Class = 'BioSeqAct_PMCheckState',
    [int]$ActionInput = 0,
    [int]$Seconds = 300,
    [int]$EndAfter = 0,
    [ValidateSet('skip', 'full')] [string]$Pace = 'skip',
    [int[]]$Snapshots = @(),
    [int]$Walk = 0,
    [int]$WanderAt = 0,
    [int]$TurnAt = 0,
    [int]$Lines = 0,
    [string]$Replay = '',
    [string]$Before = '',
    [string[]]$Probe = @(),
    [switch]$NoLoad,
    [switch]$All,
    [string]$Career = 'John02',
    [int]$FirstSlot = 80,
    [string]$List = '',
    [string[]]$Plans = @(),
    [int[]]$Only = @(),
    [switch]$Stances,
    [string]$Chunk = '',
    [string]$SaveDir = ''
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$saveRoot = Join-Path $env:USERPROFILE 'Documents\BioWare\Mass Effect Legendary Edition\Save\ME1'
function Console([string]$line) { (& (Join-Path $tools 'console.ps1') $line) -join "`n" }
function Say([string]$text) { Write-Output ("{0} {1}" -f (Get-Date -Format HH:mm:ss), $text) }
function Mode() { [regex]::Match((Console 'mevr convo'), 'game mode (-?\d+)').Groups[1].Value }
# A message box of the game's (a new talent, a refused load) holds the game in its GUI mode: answered with A.
function Clear-Boxes() {
    for ($i = 0; $i -lt 4 -and (Mode) -eq '7'; $i++) {
        $focused = (Console 'mevr panel') -split "`n" | Where-Object { $_ -match 'focus true' } | Select-Object -First 1
        if ($focused -notmatch 'MessageBox') { break }
        Console 'mevr guikey 12' | Out-Null
        Start-Sleep -Seconds 2
    }
}
if (-not (Get-Process MassEffect1 -ErrorAction SilentlyContinue)) { throw 'the game is not running' }

# The scan's stage walks per conversation (its actorMove: lines on which the stage moves someone; tools/conversation-scan.py), so
# -Stances never ends a scene a stage walk is still to come in. Made once from the scan's records into %LOCALAPPDATA%\MEVR.
$stageWalks = @{}
if ($Stances) {
    $scanData = Join-Path $env:LOCALAPPDATA 'MEVR\scan-all.jsonl'
    $scanCsv = Join-Path $env:LOCALAPPDATA 'MEVR\conversation-scan.csv'
    if ((Test-Path $scanData) -and (-not (Test-Path $scanCsv) -or (Get-Item $scanCsv).LastWriteTime -lt (Get-Item $scanData).LastWriteTime)) {
        & python -I (Join-Path $tools 'conversation-scan.py') $scanData --csv $scanCsv | Out-Null
    }
    if (Test-Path $scanCsv) { Import-Csv $scanCsv | ForEach-Object { $stageWalks[$_.conversation] = [int]$_.actorMove } }
}
# The scan's key of a conversation's name ("BioConversation sta30_khalisah_D.sta30_khalisah_dlg" -> sta30_khalisah).
function Conversation-Key([string]$name) {
    $k = ($name -split '[. ]')[-1].ToLowerInvariant()
    foreach ($suffix in '_dlg', '_d') { if ($k.Length -gt $suffix.Length -and $k.EndsWith($suffix)) { $k = $k.Substring(0, $k.Length - $suffix.Length) } }
    $k
}

# The mod's log (the lines since a time): a scene that plays as barks is told by its voice there (the census below).
function Log-Since([datetime]$since) {
    $game = Get-Process MassEffect1 -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $game) { return @() }
    $log = Join-Path (Split-Path -Parent $game.Path) 'Logs\LE1MEVR.log'
    $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    try { $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    $from = $since.ToString('HH:mm:ss')
    @($text -split "`n" | Where-Object { $_.Length -gt 10 -and $_.Substring(1, 8) -ge $from })
}

# -states: the named streaming states its chunk is shown under (streamed in when the action isn't loaded); -barks: a scene the
# game plays as barks (ambient, or flowed by the mod), with no conversation mode to wait for: its voice is the sign it played.
function Play-Scene([string]$save, [string]$class, [string]$action, [int]$actionInput, [string]$title, [string[]]$states = @(), [bool]$barks = $false) {
    $script:byItself = $false
    Clear-Boxes
    if (-not $NoLoad) {
        $source = $save
        if (-not (Test-Path -LiteralPath $source)) {
            $leaf = $(if ($save.EndsWith('.pcsav')) { $save } else { "$save.pcsav" })
            $source = @(Get-ChildItem -LiteralPath $saveRoot -Recurse -File -Filter $leaf | Select-Object -First 1 -ExpandProperty FullName)[0]
            if (-not $source) { Say "no save $leaf under $saveRoot"; return $false }
        }
        $m = [regex]::Match((Console "mevr instances BioSaveGame`nmevr props 0"), '"m_sCharacterID": "([^"]*)"')
        if (-not $m.Success -or -not $m.Groups[1].Value) { throw 'no career is loaded in the game (load any save first)' }
        $career = $m.Groups[1].Value
        $quick = Join-Path $saveRoot "$career\$($career)_QuickSave.pcsav"
        $stamp = if (Test-Path -LiteralPath $quick) { (Get-Item -LiteralPath $quick).LastWriteTime } else { [datetime]::MinValue }
        Console 'QuickSave' | Out-Null
        $own = $false
        for ($i = 0; $i -lt 15 -and -not $own; $i++) { Start-Sleep 1; $own = (Test-Path -LiteralPath $quick) -and (Get-Item -LiteralPath $quick).LastWriteTime -gt $stamp }
        if (-not $own -and -not (Test-Path -LiteralPath $quick)) { Say "the game wrote no quicksave for $career (a conversation, combat or a menu refuses it) and has none: QuickLoad would refuse the save"; return $false }
        Copy-Item -LiteralPath $source -Destination $quick -Force
        Console 'QuickLoad' | Out-Null
        $loadedAt = (Get-Date).AddSeconds(-1)
        Say "loading $(Split-Path -Leaf $source) through $career's quicksave$(if ($title) { ": $title" })"
        # Loaded: on foot again, twice in a row, after the load's own modes.
        Start-Sleep -Seconds 8
        $onFoot = 0
        for ($i = 0; $i -lt 60 -and $onFoot -lt 2; $i++) {
            Start-Sleep -Seconds 2
            $mode = Mode
            if ($mode -eq '7') {
                Clear-Boxes; $mode = Mode
                # A save on an uncharted world loads into the landing's squad screen (PartySelect, 2026-11-11: four saves
                # waited on it for two minutes each): the screen's own FinalizeParty, as the player's pick would.
                if ($mode -eq '7' -and ((Console 'mevr panel') -split "`n" | Where-Object { $_ -match 'PartySelect .*focus true' })) {
                    Console 'mevr vehicle party' | Out-Null; Say "the landing's squad screen: the party finalized"; Start-Sleep -Seconds 4; $mode = Mode
                }
                # Shepard dead as the save loads (Therum's troop drop, 2026-11-11: four minutes on the game's GameOver screen).
                if ($mode -eq '7' -and ((Console 'mevr panel') -split "`n" | Where-Object { $_ -match 'GameOver .*focus true' })) {
                    Say 'the game is over as the save loads (Shepard died: the GameOver screen)'; return $false
                }
            }
            # A save that stands the player inside the scene's trigger starts it as it loads (Noveria's Benezia and Therum's
            # Liara, 2026-11-11): that is the scene, played as the game plays it, not a save that never came up.
            # A conversation's test waits through a cinematic (mode 6): only its conversation, by name, counts (the second Codex
            # review, 2026-11-11); a scripted walk's test takes the cinematic mode as its scene beginning.
            if ($mode -eq '5' -or ($mode -eq '6' -and (-not $title -or $title.StartsWith('moment_')))) { $script:byItself = $true; break }
            $onFoot = $(if ($mode -eq '0' -or $mode -eq '1') { $onFoot + 1 } else { 0 })   # the Mako (1) after a landing too
        }
        # Only the scene asked for counts (the Codex review, 2026-11-11: any conversation on load stood in for it). A conversation
        # is told by its name; a scripted walk may follow a conversation of its own, so a moment's run takes what begins.
        if ($script:byItself -and $title -and -not $title.StartsWith('moment_')) {
            $now = Console 'mevr convo'
            if ((Mode) -eq '5' -and $now -notmatch [regex]::Escape($title)) {
                Say "another scene began as the save loaded ($([regex]::Match($now, 'BioConversation (\S+)').Groups[1].Value)): not $title"; return $false
            }
        }
        if ($script:byItself) { Say 'the scene began by itself as the save loaded (its trigger at the player): not fired, driven' }
        elseif ($onFoot -lt 2) { Say 'the save did not come up on foot in two minutes'; return $false }
        Start-Sleep -Seconds 3
    }
    # A scene that began as the save loaded is judged from the load: its opening is in the log before the runner saw it.
    $began = $(if ($script:byItself) { $loadedAt } else { (Get-Date).AddSeconds(-2) }).ToString('HH:mm:ss')
    if (-not $script:byItself) {
    $tail = ($action -split '\.')[-1]
    $listing = Console "mevr instances $class $tail"
    $index = -1
    foreach ($line in $listing -split "`n") {
        $lm = [regex]::Match($line, '^\[(\d+)\]\s+(\S+)\s+(\S+)')
        if ($lm.Success -and $lm.Groups[3].Value.EndsWith($action)) { $index = [int]$lm.Groups[1].Value }
    }
    # Its chunk is shown only under a streaming state the plot sets (the Normandy's huddle, Feros after the Thorian): the save
    # carries the states of the level it was written from, so they are turned on one by one until the action is there.
    foreach ($state in @($states)) {
        if ($index -ge 0 -or -not $state) { break }
        Console "mevr stream state $state on" | Out-Null
        Say "$class ...$action is not loaded: streaming state $state on"
        for ($w = 0; $w -lt 10 -and $index -lt 0; $w++) {
            Start-Sleep -Seconds 2
            foreach ($line in (Console "mevr instances $class $tail") -split "`n") {
                $lm = [regex]::Match($line, '^\[(\d+)\]\s+(\S+)\s+(\S+)')
                if ($lm.Success -and $lm.Groups[3].Value.EndsWith($action)) { $index = [int]$lm.Groups[1].Value }
            }
        }
    }
    # -Chunk: a chunk a streaming volume's plain state shows (the save stands outside it): that volume touched, as the player's
    # walk in would (tools/conversation-launch.ps1's way; never `streamlevelin`).
    if ($index -lt 0 -and $Chunk) {
        $chunkRe = "(^|_)$([regex]::Escape($Chunk))(_|$)"
        $volume = $null; $current = $null
        foreach ($line in ((Console 'mevr stream list') -split "`n")) {
            $m = [regex]::Match($line, '^\[(\d+)\] (\S+) at ')
            if ($m.Success) { $current = $m.Groups[2].Value; continue }
            $st = [regex]::Match($line, '^\s*state (\S+) \(in [^)]*\): visible ([^;]*);')
            if ($current -and $st.Success -and ($st.Groups[1].Value -in 'None', 'Default') -and @(($st.Groups[2].Value -split ',') -match $chunkRe)) { $volume = $current; break }
        }
        if ($volume) {
            Console "mevr stream touch $volume" | Out-Null
            Say "$class ...$action is not loaded: $volume touched (it shows $Chunk)"
            for ($w = 0; $w -lt 15 -and $index -lt 0; $w++) {
                Start-Sleep -Seconds 2
                foreach ($line in (Console "mevr instances $class $tail") -split "`n") {
                    $lm = [regex]::Match($line, '^\[(\d+)\]\s+(\S+)\s+(\S+)')
                    if ($lm.Success -and $lm.Groups[3].Value.EndsWith($action)) { $index = [int]$lm.Groups[1].Value }
                }
            }
            if ($index -ge 0) { Start-Sleep -Seconds 3 }   # its floor and people shown before anything is fired
        } else { Say "no streaming volume's plain state shows $Chunk" }
    }
    if ($index -lt 0) { Say "$class ...$action is not loaded"; return $false }
    $started = Get-Date
    if ($Before) { Console $Before | Out-Null }   # e.g. a call trace that must cover the scene's first frames
    if ($barks) { Console 'mevr voices census 14' | Out-Null }   # every sound source and its cue for 14 s
    if ($action -like 'GalaxyMap.Comm_Room_Dialogs.*') {
        # A call at the galaxy map comes from the console's use, which plays the pending transmission and opens the map after it
        # (research/69 "Hackett's call and the surveyor, run again"): fired directly, the map's opening wasn't the use's.
        Console 'mevr galaxy use' | Out-Null
        Say "used the galaxy map's console (its sequence plays $action)"
    } else {
        Console "mevr activate $index $actionInput" | Out-Null
        Say "fired $action (input $actionInput)"
    }
    if ($barks) {
        # Barks: no conversation mode. Its lines are told by their cues in the census (the conversation's package name).
        Start-Sleep -Seconds 17
        $name = ($title -replace '_dlg$', '').ToLowerInvariant()
        $heard = @(Log-Since $started | Where-Object { $_ -match 'voice census:   ' -and $_.ToLowerInvariant().Contains($name) })
        if ($heard.Count -gt 0) { Say ("played as barks: {0} of its lines heard (the voice census)" -f $heard.Count); return $true }
        Say 'played as barks? none of its lines heard in 14 s (the voice census)'; return $false
    }
    } else { $started = Get-Date }
    # The test's inputs whichever way the scene began (they were skipped for a scene that started by itself).
    if ($Walk -gt 0) { Console "mevr convo walk 0 1 $Walk" | Out-Null }
    if ($Replay) { & (Join-Path $tools 'replay.ps1') -Path $Replay -Loop -Inputs *> $null }
    if ($Stances) { Console 'mevr convo stances on' | Out-Null }

    $shots = @($Snapshots | Sort-Object)
    $probes = @($Probe | ForEach-Object { $i = $_.IndexOf(':'); [pscustomobject]@{ At = [double]$_.Substring(0, $i); Line = $_.Substring($i + 1) } } | Sort-Object At)
    $quiet = 0; $seen = $false; $lastKey = ''; $convoSince = $null; $ok = $true; $wandered = $false; $judged = @{}; $turnStep = 0; $linesSeen = 0
    while (((Get-Date) - $started).TotalSeconds -lt $Seconds) {
        Start-Sleep -Milliseconds 1500
        $elapsed = ((Get-Date) - $started).TotalSeconds
        while ($probes.Count -gt 0 -and $elapsed -ge $probes[0].At) {
            Say ("probe at {0:0.0} s: {1}" -f $elapsed, $probes[0].Line)
            (Console $probes[0].Line) -split "`n" | ForEach-Object { Write-Output "    $_" }
            $probes = @($probes | Select-Object -Skip 1)
        }
        if ($shots.Count -gt 0 -and $elapsed -ge $shots[0]) {
            & (Join-Path $tools 'snapshot.ps1') -Tag ("save-{0:000}" -f [int]$shots[0]) *> $null
            Say ("snapshot at {0:0} s" -f $elapsed)
            $shots = @($shots | Select-Object -Skip 1)
        }
        $state = Console 'mevr convo'
        $mode = [regex]::Match($state, 'game mode (-?\d+)').Groups[1].Value
        $playing = ($state -match 'conversation \d+ \(') -or $mode -eq '6' -or $mode -eq '5'
        if ($playing) { $seen = $true; $quiet = 0 } else { $quiet++ }
        if ($seen -and $quiet -ge 5) { Say ("nothing has played for 7 s: done after {0:0} s" -f $elapsed); break }
        if (-not $seen -and $elapsed -gt 25) { Say 'nothing came of the action in 25 s (a plot check on the way?)'; $ok = $false; break }
        if ($state -match 'conversation (\d+) \(BioConversation ([^)]+)\), ([\d.]+) s, .*entry (-?\d+), wheel (up|down)') {
            if ($null -eq $convoSince) { $convoSince = Get-Date }
            if ($EndAfter -gt 0 -and ((Get-Date) - $convoSince).TotalSeconds -ge $EndAfter) {
                Console 'mevr convo end' | Out-Null
                Say ("the conversation ended {0} s in (only its opening was wanted)" -f $EndAfter)
                $convoSince = $null
                continue
            }
            $key = "$($Matches[1])/$($Matches[4])"
            $wheelUp = $Matches[5] -eq 'up'
            $number = $Matches[1]; $convName = $Matches[2]; $entry = $Matches[4]
            if ($Stances -and $Lines -gt 0 -and $linesSeen -lt $Lines -and $key -ne $lastKey -and -not $wheelUp -and $entry -ne '-1') {
                $linesSeen++
                Console "mevr convo stance line-$entry" | Out-Null   # whom the heads turn to on this line
            }
            if ($TurnAt -gt 0 -and $turnStep -lt 3 -and -not $wheelUp -and ((Get-Date) - $convoSince).TotalSeconds -ge ($TurnAt + 2 * $turnStep)) {
                # The right stick's turn: does the player's eye stay where it was, and Shepard's body under it?
                $degrees = @(90, 90, -180)[$turnStep]
                $turned = Console "mevr convo turn $degrees"
                Start-Sleep -Milliseconds 700
                if ($Stances) { Console ("mevr convo stance turned-{0}" -f @(90, 180, 0)[$turnStep]) | Out-Null }
                Say ("turned {0} deg as by the right stick: {1}" -f $degrees, ($turned -replace '^convo: ', ''))
                $turnStep++
            }
            if ($Stances -and -not $judged.ContainsKey($number) -and ($TurnAt -eq 0 -or $turnStep -ge 3) -and ($Lines -eq 0 -or $linesSeen -ge $Lines) -and ((Get-Date) - $convoSince).TotalSeconds -ge 4) {
                # Once its opening has settled and nobody walks any more: does anything still have to move in this scene?
                $ahead = Console 'mevr convo ahead'
                $walksNow = $ahead -match ' now '
                if (-not $walksNow -or ((Get-Date) - $convoSince).TotalSeconds -ge 40) {
                    $judged[$number] = $true
                    $stage = $stageWalks[(Conversation-Key $convName)]
                    if ($ahead -match 'movement ahead: none' -and -not ($stage -gt 0)) {
                        $written = Console 'mevr convo stance settled'
                        Console 'mevr convo end' | Out-Null
                        Say ("nothing moves in this scene from here on: its stance written ({0}), the scene ended" -f ([regex]::Match($written, 'stances\\([^ ]+)').Groups[1].Value))
                        $convoSince = $null
                        continue
                    }
                    Say ("played on: {0}{1}" -f ($ahead -replace '^convo: ', ''), $(if ($stage -gt 0) { "; the stage walks someone on $stage lines (the scan)" } else { '' }))
                }
            }
            if ($WanderAt -gt 0 -and -not $wandered -and -not $wheelUp -and ((Get-Date) - $convoSince).TotalSeconds -ge $WanderAt) {
                # A player who wanders off mid-conversation: the stick sideways for 3 s (about 2.7 m at the conversation's
                # walking pace), then standing still: the conversational circle should gather round again.
                Console 'mevr convo walk 1 0 3' | Out-Null
                Say ("wandered off: the stick to the right for 3 s, {0:0} s into the conversation" -f ((Get-Date) - $convoSince).TotalSeconds)
                $wandered = $true
            }
            if ($wheelUp) {
                Console 'mevr convo pick 0' | Out-Null
            } elseif ($Pace -eq 'skip' -and $key -eq $lastKey) {
                Console 'mevr convo skip' | Out-Null   # a line two seconds in is skipped; a new line gets its two seconds first
            }
            $lastKey = $key
        } else { $convoSince = $null }
    }
    if ($Replay) { & (Join-Path $tools 'replay.ps1') -Stop *> $null }
    & python -I (Join-Path $tools 'conversation-session.py') --since $began | Select-Object -Skip 1
    if ($Stances) {
        Start-Sleep -Seconds 1   # the end's stance
        & python -I (Join-Path $tools 'conversation-stance.py') --since ((Get-Date -Format 'yyyy-MM-dd') + 'T' + $began) | ForEach-Object { Say "from above: $_" }
    }
    return $ok
}

if ($All) {
    if (-not $List) { $List = Join-Path $tools 'conversation-headset-sample.json' }
    if ($Plans.Count -eq 0) { $Plans = @('moments.json', 'plan-sample-1.json', 'plan-all.json') | ForEach-Object { Join-Path $env:LOCALAPPDATA "MEVR\sweep\$_" } | Where-Object { Test-Path $_ } }
    $rows = (& python -I (Join-Path $tools 'conversation-sweep.py') actions $List @Plans --first-slot $FirstSlot) | ConvertFrom-Json
    $done = 0; $failed = @()
    foreach ($row in $rows) {
        if ($Only.Count -gt 0 -and $Only -notcontains [int]$row.slot) { continue }
        Write-Output ''
        Write-Output ("=== slot {0}: {1}" -f $row.slot, $row.title)
        if (-not $row.tail) { Say 'no plan knows its starting action: skipped'; $failed += $row.slot; continue }
        $saveName = "{0}_{1}" -f $Career, $row.slot
        if ($SaveDir) { $saveName = Join-Path $SaveDir "$saveName.pcsav" }   # saves kept outside the careers' folders
        $result = Play-Scene $saveName $row.class $row.tail ([int]$row.input) $row.scene @($row.streamStates) ([bool]$row.barks)
        $result | Where-Object { $_ -isnot [bool] }
        if (@($result)[-1] -ne $true) { $failed += $row.slot }
        $done++
    }
    Write-Output ''
    Say ("{0} scenes played from their saves; {1}" -f $done, $(if ($failed.Count) { "did not start: slots $($failed -join ', ')" } else { 'all started' }))
} else {
    if (-not $Action) { throw '-Action <sequence path tail>, or -All' }
    $result = Play-Scene $Save $Class $Action $ActionInput ''
    $result | Where-Object { $_ -isnot [bool] }
    if (@($result)[-1] -ne $true) { exit 1 }
}
