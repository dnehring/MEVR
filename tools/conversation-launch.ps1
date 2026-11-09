# SPDX-License-Identifier: MIT
# Conversation launcher (LE1, research/59 "The launcher"): starts any of the game's conversations in a running game without a
# save, to look at it in first person (or on the screen) within a minute or two.
#
#   tools/conversation-launch.ps1 -List mira                 the catalogue's entries whose name holds "mira"
#   tools/conversation-launch.ps1 ice50_mira                 open Noveria, stream Peak 15's chunk, stand by Mira, start it
#   tools/conversation-launch.ps1 sta20_udina -Party         ... with a squad (the party screen's own FinalizeParty)
#   tools/conversation-launch.ps1 ice20_anoleis -NoOpen      the level is already open: stream and start only
#   tools/conversation-launch.ps1 ice50_mira -World screen   on the 3D screen instead (mevr convo world <mode>; the setting after)
#   tools/conversation-launch.ps1 ice50_mira -Snapshot       a snapshot 4 s into the conversation
#   tools/conversation-launch.ps1 sta20_avina_keeper -Save John02_21
#                                                            a save from the career's own plot state instead of `open`:
#                                                            John02\John02_MEVR.pcsav at the owner (below)
#   tools/conversation-launch.ps1 -RestoreSaves              put the Save folder back as the last -Save run found it
#
# How: the catalogue (python tools/conversation-scan.py scan.jsonl --catalogue %LOCALAPPDATA%\MEVR\conversation-catalogue.json;
# tools/conversation-scan writes scan.jsonl from the game's packages, no game needed) knows, per conversation start, the persistent
# level, the chunk the start lives in, its owner's place and facing (when the owner is placed in the level) and its stage. The
# launcher opens the level (`disconnect`, `open`), waits for gameplay (a map that waits in the party screen gets `mevr vehicle
# party`), streams the chunk in (a BioTriggerStream volume whose plain state shows it: `mevr stream touch`; a chunk the plot shows
# through a named streaming state, like the medical bay's: `mevr stream state <name> on`, then the volume), waits until `mevr
# stream chunks` says it is loaded and visible, moves Shepard to 1.5 m in front of the owner, 50 units up (the game drops him onto
# the floor), facing it (`mevr setloc`, the console's `set ... Rotation`), finds the start action (`mevr instances BioSeqAct_StartConversation <tail>`) and fires it
# (`mevr activate <n> 0`, as its Kismet event would, plot conditions skipped). Needs the game running with the mod (the fake
# headset: tools/fake-headset.ps1 -On; tools/launch-game.ps1 -Steam). A fresh level's plot state is the story's start: an owner
# spawned by the plot isn't there (the catalogue has no place for it: `ownerAt` empty), and lines can play out of their story.
# Opening maps autosaves over the player's career: back up the saves first (docs/knowledge/gotchas/test-runs-overwrite-saves.md).
#
# -Save <career save> (research/59 "The launcher", "A save instead of open"): `open` starts the level from the story's start,
# so the plot isn't the career's (a scene's conditional refuses, the Citadel's arrival never plays). Instead, tools/le1-save.py
# writes <career>_MEVR.pcsav beside the career's saves (John02\John02_21 -> John02\John02_MEVR.pcsav): the career's whole plot
# and world state, the catalogue's persistent level (`map`), Shepard 1.5 m in front of the owner facing it, the newest time, so
# the Load screen lists it. It backs up the Save folder first (%LOCALAPPDATA%\MEVR\save-backups; -RestoreSaves puts back what
# changed and deletes the *_MEVR saves). Measured 2026-11-09: the title's Enter resumes the profile's last save (GamerProfile
# names it), never a newer one, and the game lists a career's saves once at its start, so the first -Save of a career runs with
# the game closed (or the game is restarted after it); after that the file exists and later runs just rewrite it. With the game
# running, the launcher loads it itself through the game's own Load screen (the pause menu's second entry, `mevr gui Menu` and
# the GUI's pad events; the record is found by file name in BioSFHandler_LoadGame.lstRecords and the confirm box's context
# checked against it), waits for Shepard on foot at the place, then streams the chunk and fires the start as with -NoOpen. The
# plot conditions the start checks are the career's: a scene the career has played may refuse to play again.
param(
    [Parameter(Position = 0)] [string]$Name = '',
    [string]$List = '',
    [string]$Catalogue = (Join-Path $env:LOCALAPPDATA 'MEVR\conversation-catalogue.json'),
    [switch]$Party,
    [switch]$NoOpen,
    [string]$World = '',
    [switch]$Snapshot,
    [switch]$NoGod,
    [int]$LoadSeconds = 240,
    [int]$StreamSeconds = 60,
    [double]$StandM = 1.5,
    [string]$Save = '',
    [string]$Area = '',
    [switch]$RestoreSaves,
    [string]$SaveRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition\Save\ME1')
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot

$backupRoot = Join-Path $env:LOCALAPPDATA 'MEVR\save-backups'
if ($RestoreSaves) {
    $last = Get-ChildItem -LiteralPath $backupRoot -Directory -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
    if (-not $last) { throw "no backup in $backupRoot" }
    Write-Host "restoring $SaveRoot from $($last.FullName)"
    foreach ($f in Get-ChildItem -LiteralPath $last.FullName -File -Recurse) {
        $rel = $f.FullName.Substring($last.FullName.Length).TrimStart('\')
        $dst = Join-Path $SaveRoot $rel
        if (-not (Test-Path -LiteralPath $dst) -or (Get-FileHash -LiteralPath $dst).Hash -ne (Get-FileHash -LiteralPath $f.FullName).Hash) {
            New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
            Write-Host "   put back $rel"
        }
    }
    foreach ($f in Get-ChildItem -LiteralPath $SaveRoot -File -Recurse -Filter '*_MEVR.pcsav') { Remove-Item -LiteralPath $f.FullName; Write-Host "   deleted $($f.Name)" }
    foreach ($f in Get-ChildItem -LiteralPath $SaveRoot -File -Recurse) {
        $rel = $f.FullName.Substring($SaveRoot.Length).TrimStart('\')
        if (-not (Test-Path -LiteralPath (Join-Path $last.FullName $rel))) { Write-Warning "$rel is newer than the backup: left as it is" }
    }
    return
}

if (-not (Test-Path -LiteralPath $Catalogue)) { throw "no catalogue at $Catalogue (python tools/conversation-scan.py scan.jsonl --catalogue <that path>)" }
$entries = Get-Content -LiteralPath $Catalogue -Raw | ConvertFrom-Json

if ($List) {
    $entries | Where-Object { $_.conversation -like "*$List*" -or $_.comment -like "*$List*" } |
        Select-Object conversation, map, chunk, ownerClass, ownerTag, @{ n = 'placed'; e = { $null -ne $_.ownerAt } }, comment |
        Sort-Object conversation -Unique | Format-Table -AutoSize | Out-String -Width 200
    return
}
if (-not $Name) { throw 'give a conversation (tools/conversation-launch.ps1 -List <text> finds one)' }

# The entry: an exact name first, else the first whose name holds it; among a conversation's starts the placed owner first (the
# catalogue is sorted so).
$matches_ = @($entries | Where-Object { $_.conversation -eq $Name })
if ($matches_.Count -eq 0) { $matches_ = @($entries | Where-Object { $_.conversation -like "*$Name*" }) }
if ($matches_.Count -eq 0) { throw "no conversation like '$Name' in the catalogue" }
$entry = $matches_[0]
Write-Host ("{0}: {1}, chunk {2}, owner {3} {4}{5}, stage {6}" -f $entry.conversation, $entry.map, $entry.chunk, $entry.ownerClass, $entry.ownerTag,
    $(if ($entry.ownerAt) { ' placed' } else { ' (not placed in the level: the plot spawns it)' }), $(if ($entry.stage) { 'yes' } else { 'none' }))

function Console([string]$line) { ((& (Join-Path $tools 'console.ps1') $line 2>$null) | Out-String).Trim() }
function State() {
    $r = Console 'mevr convo'
    $m = [regex]::Match($r, 'game mode (-?\d+)')
    [pscustomobject]@{ mode = $(if ($m.Success) { [int]$m.Groups[1].Value } else { -2 }); convo = ($r -match 'conversation \d+ \('); text = $r }
}
# Waits for gameplay (mode 0 three polls in a row). A map waiting in the party screen (mode 7 for 20 s) is sent on with the
# squad screen's own FinalizeParty (which also gives Shepard a squad: -Party).
function Settle([int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $steady = 0
    $guiSince = $null
    while ((Get-Date) -lt $deadline -and $steady -lt 3) {
        $s = State
        if ($s.convo) { Console 'mevr convo end' | Out-Null; Start-Sleep -Seconds 3; $steady = 0; continue }
        if ($s.mode -eq 0) { $steady++ } else { $steady = 0 }
        if ($s.mode -eq 7) {
            if ($null -eq $guiSince) { $guiSince = Get-Date }
            elseif (((Get-Date) - $guiSince).TotalSeconds -gt 20) { Write-Host '   the party screen: FinalizeParty'; Console 'mevr vehicle party' | Out-Null; $guiSince = $null; Start-Sleep -Seconds 5 }
        } else { $guiSince = $null }
        Start-Sleep -Seconds 3
    }
    return $steady -ge 3
}

# Where Shepard stands: 1.5 m in front of the owner (its placed spot, along its facing), facing it; else on the stage.
function Stand-Place() {
    if ($entry.ownerAt) {
        $at = $entry.ownerAt
        $ownerYaw = [double]($(if ($null -ne $entry.ownerYaw) { $entry.ownerYaw } else { 0 }))
        $yaw = $ownerYaw * [math]::PI / 180
        $d = $StandM * 100
        return [pscustomobject]@{ x = [double]$at[0] + [math]::Cos($yaw) * $d; y = [double]$at[1] + [math]::Sin($yaw) * $d
            z = [double]$at[2]; face = ((($ownerYaw + 180) % 360) + 360) % 360; what = "$StandM m in front of the owner" }
    }
    if ($entry.stageAt) {
        $st = $entry.stageAt
        return [pscustomobject]@{ x = [double]$st[0]; y = [double]$st[1]; z = [double]$st[2] + 90; face = $null; what = 'the stage' }
    }
    return $null
}
function Place-Shepard() {
    $p = Stand-Place
    if ($null -eq $p) { Write-Warning 'no place known for the owner or the stage: Shepard stays where he is'; return }
    # 50 units above (the game drops Shepard onto the floor; at floor level he fell through a fresh chunk's).
    Write-Host ("   Shepard to {0:0} {1:0} {2:0} ({3}, 0.5 m up)" -f $p.x, $p.y, ($p.z + 50), $p.what)
    if (-not $NoGod) { Console 'god' | Out-Null }  # Virmire's teleport killed Shepard (the GameOver screen, 2026-11-09)
    Console ("mevr setloc {0:0} {1:0} {2:0}" -f $p.x, $p.y, ($p.z + 50)) | Out-Null
    if ($null -ne $p.face) {
        # Facing the owner (the engine's console `set` on the pawn and the controller, whose rotation the on-foot camera
        # follows): the held view in first person starts from the on-foot view's heading (the first run faced 107 degrees
        # away from Mira).
        $face = [int][math]::Round($p.face * 65536 / 360) % 65536
        Console ("set SFXPawn_Player Rotation (Pitch=0,Yaw={0},Roll=0)" -f $face) | Out-Null
        Console ("set BioPlayerController Rotation (Pitch=0,Yaw={0},Roll=0)" -f $face) | Out-Null
    }
}
function Player-At() {
    $r = Console 'mevr loc'
    $m = [regex]::Match($r, '(-?\d+(?:\.\d+)?)[ ,]+(-?\d+(?:\.\d+)?)[ ,]+(-?\d+(?:\.\d+)?)')
    if ($m.Success) { return [pscustomobject]@{ x = [double]$m.Groups[1].Value; y = [double]$m.Groups[2].Value; z = [double]$m.Groups[3].Value; text = $r } }
    return [pscustomobject]@{ x = [double]::NaN; y = [double]::NaN; z = [double]::NaN; text = $r }
}

# The game's Load screen, driven like a pad (research/59 "A save instead of open"): the pause menu (ShowMenuGui), down + A until
# the Load panel has the focus (its second entry on foot and in the Mako), the record found by file name in the handler's
# lstRecords, down and A (the list has no focused record before a pad direction), the confirm box's nInputCallbackContext (the record's index) checked and the focus moved
# until it is ours, then A. The list is the one the game built at its start.
function Panels() { Console 'mevr panel' }
function FocusedPanel() { $m = [regex]::Match((Panels), '(?m)^\s*(\S+) \(BioSFPanel_\d+\):[^\n]*focus true[^\n]*handler (\S+) (\S+)'); if ($m.Success) { @($m.Groups[1].Value, $m.Groups[3].Value) } else { @('', '') } }
function HandlerProps([string]$class, [string]$fullName) {
    $list = Console "mevr instances $class"
    $m = [regex]::Match($list, "(?m)^\[(\d+)\] $class $([regex]::Escape($fullName))")
    if (-not $m.Success) { return $null }
    $text = Console ("mevr instances $class`nmevr props " + $m.Groups[1].Value)
    $start = $text.IndexOf('{"name"')
    if ($start -lt 0) { return $null }
    return ($text.Substring($start, $text.LastIndexOf('}') - $start + 1) | ConvertFrom-Json).props
}
function Load-ThroughMenu([string]$stem) {
    Console 'mevr gui Menu' | Out-Null
    Start-Sleep -Seconds 2
    $focus = FocusedPanel
    for ($i = 0; $i -lt 6 -and $focus[0] -ne 'Load'; $i++) {
        Console 'mevr guikey 8 w 30 w w 12' | Out-Null
        Start-Sleep -Seconds 3
        $focus = FocusedPanel
        if ($focus[0] -ne 'Load' -and $focus[0] -ne 'MenuBrowser') { Console 'mevr guikey 13' | Out-Null; Start-Sleep -Seconds 2 }
    }
    if ($focus[0] -ne 'Load') { throw "the pause menu's Load screen didn't open (focused: $($focus[0]))" }
    $records = @((HandlerProps 'BioSFHandler_LoadGame' $focus[1]).lstRecords)
    $want = -1
    for ($i = 0; $i -lt $records.Count; $i++) { if ($records[$i].m_filename -eq $stem) { $want = $i } }
    if ($want -lt 0) {
        Console 'mevr guikey 13 w w w w 13' | Out-Null
        throw "the Load screen doesn't list $stem ($(($records | ForEach-Object { $_.m_filename }) -join ', ')): the game lists saves once at its start; restart it and run this again"
    }
    for ($try = 0; $try -lt 4; $try++) {
        # The list has no focused record until a pad direction arrives: down first (the box's context then says where it went).
        Console $(if ($try -eq 0) { 'mevr guikey 8 w 30 w w 12' } else { 'mevr guikey 12' }) | Out-Null
        Start-Sleep -Milliseconds 500
        Start-Sleep -Seconds 2
        $box = FocusedPanel
        if ($box[0] -ne 'MessageBox') { throw "no confirm box after A on the Load screen (focused: $($box[0]))" }
        $context = [int](HandlerProps 'BioSFHandler_MessageBox' $box[1]).nInputCallbackContext
        Write-Host "   the Load screen's confirm box is for record $context ($($records[$context].m_filename)); $stem is record $want"
        if ($context -eq $want) {
            Write-Host "   the Load screen's record $want ($stem) confirmed"
            Console 'mevr guikey 12' | Out-Null
            return
        }
        # B closes the box; the focus moves from the record the box was for to ours.
        # (B in a request of its own: moves sent while the box closes are lost.)
        Console 'mevr guikey 13' | Out-Null
        Start-Sleep -Seconds 2
        $keys = $(if ($want -lt $context) { '11 w 33 w w' } else { '8 w 30 w w' })
        Console ('mevr guikey' + (' ' + $keys) * [math]::Abs($context - $want)) | Out-Null
        Start-Sleep -Seconds 2
    }
    throw "couldn't put the Load screen's focus on $stem"
}

# -Save: a save at the owner instead of `open`.
if ($Save) {
    $source = $Save
    if (-not (Test-Path -LiteralPath $source)) {
        $leaf = $(if ($Save.EndsWith('.pcsav')) { $Save } else { "$Save.pcsav" })
        $source = @(Get-ChildItem -LiteralPath $SaveRoot -Recurse -File -Filter $leaf | Select-Object -First 1 -ExpandProperty FullName)[0]
        if (-not $source) { throw "no save $leaf under $SaveRoot" }
    }
    $career = Split-Path -Leaf (Split-Path $source)
    $target = Join-Path (Split-Path $source) "$($career)_MEVR.pcsav"
    $p = Stand-Place
    if ($null -eq $p) { throw "$($entry.conversation): no place known for the owner or the stage (ownerAt and stageAt empty)" }
    $face = $(if ($null -ne $p.face) { $p.face } else { 0 })
    # The Save folder first (the game autosaves into the career on its own: docs/knowledge/gotchas/test-runs-overwrite-saves.md).
    $backup = Join-Path $backupRoot (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss')
    New-Item -ItemType Directory -Force $backup | Out-Null
    Copy-Item -Path (Join-Path $SaveRoot '*') -Destination $backup -Recurse -Force
    Write-Host "   the Save folder backed up to $backup (tools/conversation-launch.ps1 -RestoreSaves puts it back)"
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $writeArgs = @('-I', (Join-Path $tools 'le1-save.py'), 'write', $source, $target, '--level', $entry.map, '--location',
        $p.x.ToString('0.0', $inv), $p.y.ToString('0.0', $inv), ($p.z + 20).ToString('0.0', $inv), '--yaw', ([double]$face).ToString('0.0', $inv), '--force')
    if ($Area) { $writeArgs += @('--area', $Area) }
    $out = & python @writeArgs 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "tools/le1-save.py failed: $out" }
    Write-Host ("   wrote {0} from {1}: {2}, Shepard at {3:0} {4:0} {5:0} ({6}), facing {7:0} deg" -f $target, (Split-Path -Leaf $source), $entry.map, $p.x, $p.y, $p.z, $p.what, $face)
    if (-not (Get-Process MassEffect1 -ErrorAction SilentlyContinue)) {
        Write-Host "   the game is not running: start it (tools/launch-game.ps1; its Enter resumes the career's last save, not this one)"
        Write-Host '   and run this again: the game then lists the file, and the launcher rewrites it and loads it through the Load screen.'

        return
    }
    Load-ThroughMenu "$($career)_MEVR"
    $deadline = (Get-Date).AddSeconds($LoadSeconds)
    $arrived = $false
    while ((Get-Date) -lt $deadline -and -not $arrived) {
        Start-Sleep -Seconds 3
        $at = Player-At
        if ([math]::Sqrt([math]::Pow($at.x - $p.x, 2) + [math]::Pow($at.y - $p.y, 2)) -lt 400 -and (State).mode -eq 0) { $arrived = $true }
    }
    if (-not $arrived) { throw "Shepard never came within 4 m of the save's place in $LoadSeconds s (mevr loc: $((Player-At).text))" }
    Write-Host "   loaded: $((Player-At).text)"
    if (-not (Settle $LoadSeconds)) { throw 'never reached gameplay (mode 0) after the load' }
    $NoOpen = $true
}

if (-not $NoOpen) {
    Write-Host "   opening $($entry.map)"
    Console 'disconnect' | Out-Null
    Start-Sleep -Seconds 15
    Console "open $($entry.map)" | Out-Null
    Start-Sleep -Seconds 5
    if (-not (Settle $LoadSeconds)) { throw "$($entry.map) never reached gameplay (mode 0) in $LoadSeconds s" }
    Start-Sleep -Seconds 3
}
if ($Party -and $NoOpen) { Console 'mevr vehicle party' | Out-Null; Start-Sleep -Seconds 3 }

# The chunk: the loaded streaming volumes and their states (`[n] Name at x y z`, then one `state <name> (in <chunk>): visible
# a,b; load ...` line per state). A plain state (None or Default) shows its chunks when the volume is touched; a named one only while the
# world's streaming state of that name is on, which the plot's Kismet sets (the medical bay's NOR10_07wake; research/59 "The
# launcher's gaps closed"): then `mevr stream state <name> on` comes first, and the catalogue's `streamStates` stand in when no
# loaded volume lists the chunk at all. Never the engine's `streamlevelin`: it froze the game.
$r = Console 'mevr stream list'
$chunkRe = "(^|_)$([regex]::Escape($entry.chunk))(_|$)"
$touchVolume = $null
$stateVolume = $null
$stateName = $null
$current = $null
foreach ($line in ($r -split "`n")) {
    $m = [regex]::Match($line, '^\[(\d+)\] (\S+) at ')
    if ($m.Success) { $current = $m.Groups[2].Value; continue }
    if ($null -eq $current) { continue }
    $s = [regex]::Match($line, '^\s*state (\S+) \(in [^)]*\): visible ([^;]*);')
    if (-not $s.Success) { continue }
    if (-not @(($s.Groups[2].Value -split ',') -match $chunkRe)) { continue }
    if ($s.Groups[1].Value -eq 'None' -or $s.Groups[1].Value -eq 'Default') { if ($null -eq $touchVolume) { $touchVolume = $current } }
    elseif ($null -eq $stateVolume) { $stateVolume = $current; $stateName = $s.Groups[1].Value }
}
$actionsBefore = Console ("mevr instances BioSeqAct_StartConversation " + $entry.actionTail.Split('.')[-1])
$streamed = $false
if ($actionsBefore -notmatch [regex]::Escape($entry.actionTail)) {
    if ($touchVolume) {
        Write-Host "   streaming $($entry.chunk) in through $touchVolume"
        Console ("mevr stream touch " + $touchVolume) | Out-Null
        $streamed = $true
    } elseif ($stateName) {
        Write-Host "   streaming $($entry.chunk) in: streaming state $stateName on, then $stateVolume touched"
        Write-Host ('   ' + (Console "mevr stream state $stateName on"))
        Start-Sleep -Seconds 1
        Console ("mevr stream touch " + $stateVolume) | Out-Null
        $streamed = $true
    } elseif ($entry.streamStates -and @($entry.streamStates).Count -gt 0) {
        $stateFromCatalogue = @($entry.streamStates)[0]
        Write-Host "   no loaded volume lists $($entry.chunk): streaming state $stateFromCatalogue on (the catalogue's)"
        Write-Host ('   ' + (Console "mevr stream state $stateFromCatalogue on"))
        $streamed = $true
    } else { Write-Warning "no streaming volume or state shows chunk $($entry.chunk) and its action isn't loaded; trying anyway" }
}
if ($streamed) {
    # Until the chunk is loaded and shown (`mevr stream chunks`: `<package> loaded=<0|1> visible=<0|1> ...` per streaming level;
    # the start's own package, or the chunk by name), at most $StreamSeconds: a teleport into a chunk whose floor isn't there yet
    # drops Shepard to the kill plane (Virmire, 2026-11-09).
    $deadline = (Get-Date).AddSeconds($StreamSeconds)
    $ready = $false
    $wanted = @($entry.package, "BIOA_$($entry.chunk)", $entry.chunk)
    while ((Get-Date) -lt $deadline -and -not $ready) {
        Start-Sleep -Seconds 2
        foreach ($line in ((Console 'mevr stream chunks') -split "`n")) {
            $m = [regex]::Match($line, '^\s*(\S+) loaded=(\d) visible=(\d)')
            if ($m.Success -and $wanted -contains $m.Groups[1].Value -and $m.Groups[2].Value -eq '1' -and $m.Groups[3].Value -eq '1') { $ready = $true; break }
        }
    }
    if ($ready) { Write-Host "   $($entry.chunk) is loaded and visible"; Start-Sleep -Seconds 2 }
    else { Write-Warning "$($entry.chunk) isn't loaded and visible after $StreamSeconds s (mevr stream chunks says what is); going on" }
}

# Shepard in front of the owner (its placed spot and facing; else the stage's spot).
Place-Shepard
Start-Sleep -Seconds 2

if ($World) { Console ("mevr convo world " + $World) | Out-Null }

# Right before firing: a conversation of the level's own that started meanwhile (Virmire's jug20_trigger after its stream) is
# ended, and a party screen that came back (Virmire's, after the stream) is sent on; else the action fires into the wrong mode.
$s = State
if ($s.convo) { Write-Host '   ending the conversation that started by itself'; Console 'mevr convo end' | Out-Null; Start-Sleep -Seconds 4; $s = State }
if ($s.mode -eq 7) { Write-Host '   the party screen is up: FinalizeParty'; Console 'mevr vehicle party' | Out-Null; Start-Sleep -Seconds 5; $s = State }
if ($s.mode -ne 0) { Write-Warning "not in gameplay before firing (game mode $($s.mode)); firing anyway" }

# The start action, by its path's tail.
$found = Console ("mevr instances BioSeqAct_StartConversation " + $entry.actionTail.Split('.')[-1])
$index = $null
foreach ($line in ($found -split "`n")) {
    $m = [regex]::Match($line, '^\[(\d+)\] BioSeqAct_StartConversation (\S+)')
    if ($m.Success -and $m.Groups[2].Value -like ("*" + $entry.actionTail)) { $index = [int]$m.Groups[1].Value; break }
}
if ($null -eq $index) { throw "the action $($entry.actionTail) isn't loaded (chunk $($entry.chunk)); `mevr instances BioSeqAct_StartConversation` lists what is" }
Write-Host "   firing [$index] $($entry.actionTail)"
Console ("mevr activate $index 0") | Out-Null
Start-Sleep -Seconds 4
$s = State
if (-not $s.convo) { Start-Sleep -Seconds 6; $s = State }  # a stage's set-up can take a few seconds
if ($s.convo) {
    Write-Host ("   started: " + ($s.text -replace '\s+', ' '))
    if ($Snapshot) { & (Join-Path $tools 'snapshot.ps1') -Tag $entry.conversation | Out-Null; Write-Host '   snapshot taken' }
} else {
    $panels = Console 'mevr panel'
    if ($panels -match 'GameOver.*visible true') { Write-Warning 'Shepard is dead (the GameOver screen): the teleport or the level killed him' }
    Write-Warning ("no conversation after the action fired: " + ($s.text -replace '\s+', ' '))
}
