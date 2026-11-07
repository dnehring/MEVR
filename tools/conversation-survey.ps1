# SPDX-License-Identifier: MIT
# Conversation survey (LE1, research/59 "The survey"): opens each map (a persistent level: BIOA_NOR00, BIOA_STA00, BIOA_ICE00...; a
# fresh Shepard), plays any conversation that starts by itself, then fires every conversation of the level's own script, one by one:
# each BioSeqAct_StartConversation action (`mevr instances BioSeqAct_StartConversation`, `mevr activate <n> 0`), as its event would.
# A conversation that starts is played to its end (a used wheel slot picked at random while the wheel is up, the line skipped
# otherwise; `mevr convo end` past -ConversationSeconds) and the mod's `convo: summary {...}` line kept. If a conversation's follow-up
# script leaves the map (a transition), the map is opened again and the survey goes on with the next action. One JSON object per
# action goes to %LOCALAPPDATA%\MEVR\conversation-survey-<stamp>.jsonl; a table at the end.
#
#   tools/conversation-survey.ps1 -Maps BIOA_NOR00,BIOA_STA00
#   tools/conversation-survey.ps1 -Maps BIOA_STA00 -Limit 5      the first 5 actions only
#   tools/conversation-survey.ps1 -Maps BIOA_ICE00 -Filter Port  the actions whose path holds "Port"
#   tools/conversation-survey.ps1 -Maps BIOA_STA00 -Lists STA20,STA30,STA60    listed through those filters (60 a listing)
#   tools/conversation-survey.ps1 -Maps BIOA_STA00 -NoOpen        what is loaded now, without opening the map again
#   tools/conversation-survey.ps1 -Maps BIOA_FRE31 -Lists BIOA_FRE -Streams    every streaming area in turn (no save needed)
#
# Needs the game running with a player (the fake headset: tools/fake-headset.ps1 -On, tools/launch-game.ps1 -Steam; `open` works
# from the front end: `tools/console.ps1 disconnect` first if a save is loaded) and `[Cinematics] conversations = world` for the world
# mode's numbers (kept, drawn, reanchors). A fired action skips the plot conditions before it: some conversations play out of their
# story. A map opened here can autosave over the player's career: back up the saves first
# (docs/knowledge/gotchas/test-runs-overwrite-saves.md). Never pipe a script into `Select-Object -First`: it stops the script there.
param(
    [Parameter(Mandatory = $true)] [string[]]$Maps,
    [int]$ConversationSeconds = 180,
    [int]$LoadSeconds = 240,
    [int]$Limit = 0,
    [string]$Filter = '',
    # `mevr instances` lists at most 60: a big map's actions are listed through several filters (its sublevels' names) and joined.
    [string[]]$Lists = @(''),
    # Survey what is loaded now (no `open`): a level streamed in since (the Citadel after the docking). Reopening is skipped too.
    [switch]$NoOpen,
    # A fresh map shows only its first area: a planet before the landing, the Citadel's arrival. With -Streams every
    # BioTriggerStream volume of the map is touched in turn (`mevr stream touch`, Shepard moved onto it first), as the
    # player's walk would, and the conversations loaded then are surveyed before the next one.
    [switch]$Streams,
    [int]$StreamSeconds = 12
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$log = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
$out = Join-Path $env:LOCALAPPDATA ("MEVR\conversation-survey-{0}.jsonl" -f (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'))
New-Item -ItemType File -Force -Path $out | Out-Null
$rng = New-Object System.Random 59

function Console([string]$line) {
    ((& (Join-Path $tools 'console.ps1') $line 2>$null) | Out-String).Trim()
}
function State() {
    $r = Console 'mevr convo'
    $m = [regex]::Match($r, 'game mode (-?\d+)')
    [pscustomobject]@{ mode = $(if ($m.Success) { [int]$m.Groups[1].Value } else { -2 }); convo = ($r -match 'conversation \d+ \(') }
}
function Actions([string]$filter) {
    # The level's conversation actions now (whose full name holds `filter`): index (for mevr activate, valid until the next
    # listing) and full name.
    $r = Console ('mevr instances BioSeqAct_StartConversation ' + $filter)
    $list = @()
    foreach ($line in ($r -split "`n")) {
        $m = [regex]::Match($line, '^\[(\d+)\] BioSeqAct_StartConversation (\S+)')
        if ($m.Success) { $list += [pscustomobject]@{ index = [int]$m.Groups[1].Value; name = $m.Groups[2].Value } }
    }
    return $list
}
function LogLines() { return (Get-Content -LiteralPath $log).Count }
function NewLines([int]$from) { return Get-Content -LiteralPath $log | Select-Object -Skip $from }

function Play([string]$map, [string]$source, [int]$mark) {
    $record = [ordered]@{ map = $map; source = $source; outcome = 'played' }
    $end = (Get-Date).AddSeconds($ConversationSeconds)
    while ((Get-Date) -lt $end -and (State).convo) {
        $picked = $false
        foreach ($slot in (0..5 | Sort-Object { $rng.Next() })) {
            $r = Console "mevr convo pick $slot"
            if ($r -match 'called true') { $picked = $true; break }
            if ($r -match "the wheel isn't up|no conversation") { break }
        }
        if (-not $picked) { Console 'mevr convo skip' | Out-Null }
        Start-Sleep -Milliseconds 1500
    }
    if ((State).convo) { $record.outcome = 'ended by mevr convo end'; Console 'mevr convo end' | Out-Null; Start-Sleep -Seconds 3 }
    Start-Sleep -Seconds 1
    $summary = NewLines $mark | Select-String -Pattern 'convo: summary (\{.*\})' | Select-Object -Last 1
    if ($summary) { $record.summary = ($summary.Matches[0].Groups[1].Value | ConvertFrom-Json) }
    $events = NewLines $mark | Select-String -Pattern 'let through|reached a drawn frame|JUMPED|kept in place' | ForEach-Object { $_.Line.Substring(15) }
    if ($events) { $record.events = @($events | Select-Object -First 30) }
    ($record | ConvertTo-Json -Compress -Depth 5) | Add-Content -LiteralPath $out
    $name = if ($record.summary) { $record.summary.name -replace '^BioConversation ', '' } else { '?' }
    Write-Host ("   {0,-60} {1,-50} {2}" -f ($source -replace '^.*Main_Sequence\.', ''), $name, $record.outcome)
}

# Waits up to `seconds` for gameplay (mode 0 three polls in a row), playing any conversation that starts by itself meanwhile.
function Settle([string]$map, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $steady = 0
    while ((Get-Date) -lt $deadline -and $steady -lt 3) {
        $mark = LogLines
        $s = State
        if ($s.convo) { Play $map '(by itself)' $mark; $steady = 0; continue }
        if ($s.mode -eq 0) { $steady++ } else { $steady = 0 }
        Start-Sleep -Seconds 3
    }
    return $steady -ge 3
}

function Open([string]$map) {
    if ($NoOpen) { return Settle $map $LoadSeconds }
    # From the front end: `open` does nothing on a game-over screen (Shepard killed during a survey, 2026-11-06).
    Console 'disconnect' | Out-Null
    Start-Sleep -Seconds 15
    Console "open $map" | Out-Null
    Start-Sleep -Seconds 5
    return Settle $map $LoadSeconds
}

$played = @{}
$tried = @{}
function Volumes() {
    $r = Console 'mevr stream list'
    # Each volume with the level chunks its states show (`visible a,b,c`): most volumes of a hub show an area another
    # already showed, so only a volume that shows a chunk not seen yet is entered (Noveria: 209 volumes).
    $list = @()
    $current = $null
    foreach ($line in ($r -split "`n")) {
        $m = [regex]::Match($line, '^\[(\d+)\] (\S+) at (-?\d+) (-?\d+) (-?\d+)')
        if ($m.Success) {
            $current = [pscustomobject]@{ name = $m.Groups[2].Value; x = $m.Groups[3].Value; y = $m.Groups[4].Value; z = $m.Groups[5].Value; chunks = @() }
            $list += $current
            continue
        }
        $v = [regex]::Match($line, 'visible ([^;]*);')
        if ($v.Success -and $null -ne $current) { $current.chunks += @($v.Groups[1].Value -split ',' | Where-Object { $_ }) }
    }
    return $list
}
function Enter($volume) {
    if ($null -eq $volume) { return }
    Console ("mevr setloc {0} {1} {2}" -f $volume.x, $volume.y, $volume.z) | Out-Null
    Console ("mevr stream touch " + $volume.name) | Out-Null
    Start-Sleep -Seconds $StreamSeconds
}
function Survey([string]$map, $volume) {
    $names = @($Lists | ForEach-Object { Actions $_ } | ForEach-Object { $_.name } | Sort-Object -Unique | Where-Object { $_ -like "*$Filter*" -and -not $tried.ContainsKey($_) })
    if ($Limit -gt 0) { $names = @($names | Select-Object -First $Limit) }
    $where = if ($volume) { " in " + $volume.name } else { '' }
    Write-Host ("   {0} new conversation actions{1}" -f $names.Count, $where)
    foreach ($name in $names) {
        $tried[$name] = $true
        if (-not (Settle $map 40)) {
            Write-Warning "not in gameplay before ${name}, the map again"
            if (-not (Open $map)) { Write-Warning "$map never reached gameplay again: stopped"; return $false }
            Enter $volume
        }
        $entry = Actions $name | Where-Object { $_.name -eq $name } | Select-Object -First 1
        if ($null -eq $entry) {
            # Gone: a conversation's follow-up left the map (or streamed its level out). Open it again and look once more.
            if (-not (Open $map)) { Write-Warning "$map never reached gameplay again: stopped"; return $false }
            Enter $volume
            $entry = Actions $name | Where-Object { $_.name -eq $name } | Select-Object -First 1
            if ($null -eq $entry) { ([ordered]@{ map = $map; source = $name; outcome = 'action not loaded' } | ConvertTo-Json -Compress) | Add-Content -LiteralPath $out; continue }
        }
        # Its conversation (the action's Conv): one already played here is skipped.
        $conv = [regex]::Match((Console ('mevr props ' + $entry.index)), '"Conv": "([^"]*)"').Groups[1].Value
        if ($conv -and $played.ContainsKey($conv)) {
            ([ordered]@{ map = $map; source = $name; outcome = 'already played'; conv = $conv } | ConvertTo-Json -Compress) | Add-Content -LiteralPath $out
            continue
        }
        if ($conv) { $played[$conv] = $true }
        $entry = Actions $name | Where-Object { $_.name -eq $name } | Select-Object -First 1
        if ($null -eq $entry) { continue }
        $mark = LogLines
        $fired = Console ("mevr activate " + $entry.index + " 0")
        Start-Sleep -Seconds 4
        if ((State).convo) {
            Play $map $name $mark
        } else {
            ([ordered]@{ map = $map; source = $name; outcome = 'no conversation'; fired = $fired } | ConvertTo-Json -Compress) | Add-Content -LiteralPath $out
            Write-Host ("   {0,-60} no conversation" -f ($name -replace '^.*Main_Sequence\.', ''))
        }
    }
    return $true
}

foreach ($map in $Maps) {
    Write-Host "== $map"
    if (-not (Open $map)) { Write-Warning "$map never reached gameplay (mode 0) in $LoadSeconds s"; continue }
    Start-Sleep -Seconds 4
    if (-not (Survey $map $null)) { continue }
    if ($Streams) {
        $volumes = @(Volumes)
        Write-Host ("   {0} streaming volumes" -f $volumes.Count)
        $seen = @{}
        foreach ($volume in $volumes) {
            $new = @($volume.chunks | Where-Object { -not $seen.ContainsKey($_) })
            if ($new.Count -eq 0) { continue }
            foreach ($c in $volume.chunks) { $seen[$c] = $true }
            if (-not (Settle $map 40)) { if (-not (Open $map)) { break } }
            Enter $volume
            if (-not (Survey $map $volume)) { break }
        }
    }
}

Write-Host "`n$out"
Get-Content -LiteralPath $out | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { $_.summary } | ForEach-Object {
    $s = $_.summary
    [pscustomobject]@{ map = $_.map; conversation = ($s.name -replace '^BioConversation ', '' -replace '^.*\.', ''); s = $s.seconds;
        stage = $s.stage; lines = $s.lines; shots = $s.shots; onShepard = $s.onShepard; cutaway = $s.cutaway; kept = $s.kept;
        drawn = $s.drawn; letGo = $s.letGo; playerJumps = $s.reanchors; maxMoveM = $s.maxMoveM; outcome = $_.outcome }
} | Format-Table -AutoSize | Out-String -Width 240
