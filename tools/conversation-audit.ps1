# SPDX-License-Identifier: MIT
# The regression set of first-person scenes, played and judged (research/69 "Auditing scenes by rule"; the skill
# `first-person-scene-audit`): every scene of tools/conversation-audit-set.json from its save, with the overhead records on and a
# record at each of the first lines, through tools/fake-headset-session.ps1 (deploy, back up, fake headset, launch, restore);
# then what each scene measured and what it did not, with a contact sheet of its eye snapshots (tools/audit-report.py), the rules'
# findings (tools/scene_audit.py) and the comparison with the baseline. About 12 minutes, 15 with -Long.
#
#   tools/conversation-audit.ps1 [-Long] [-Only sta20_chorban_dlg,ice20_opold_dlg] [-IgnoreSlot] [-NoDeploy]
#   tools/conversation-audit.ps1 -Canaries [-Canary empty-chamber,no-hands]   the broken runs, each in a game of its own: each must
#                                                     be caught (research/69 "Broken runs the audit must catch")
#   tools/conversation-audit.ps1 -WriteBaseline      the run's numbers become the baseline (after a change judged good)
#   tools/conversation-audit.ps1 -Judge <yyyy-MM-ddTHH:mm:ss> [-Session <folder>]   judge an earlier run again, no game
#   tools/conversation-audit.ps1 -Plan [-Long] [-Canaries]   print the run scripts it would play, no game
#
# Exit code 1 when the comparison finds a regression, a scene's body was not measured, an expectation failed, or a broken run
# passed clean. The game must be free (another session's run would be disturbed).
param(
    [string]$Set = '',
    [string]$Baseline = '',
    [switch]$WriteBaseline,
    [switch]$Long,
    [string[]]$Only = @(),
    [switch]$IgnoreSlot,
    [switch]$NoDeploy,
    [string]$Judge = '',
    [string]$Session = '',
    [switch]$Plan,
    [switch]$Canaries,
    [string[]]$Canary = @()
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
if (-not $Set) { $Set = Join-Path $tools 'conversation-audit-set.json' }
if (-not $Baseline) { $Baseline = Join-Path $tools 'conversation-audit-baseline.json' }

function Expand([string]$path) { [Environment]::ExpandEnvironmentVariables($path) }
function Q([string]$text) { $text -replace "'", "''" }

# Tracked hands in every scene (research/69 "The body that stayed at the mark", 2026-11-13): the fake headset has no
# controllers, so without a recording's hands arm IK and the body follower never run, and the council hearing's body left 5.7 m
# from the player in the owner's headset was a 1.3 s blip here. The set's `hands` (a scene's own, else the set's) is a
# tools/make-walk-recording.py --plan, replayed with its inputs from the moment each scene is fired, the hands held up in front of
# the eyes (--hands view) so every eye snapshot shows Shepard's drawn hands where the controllers are ("Seeing the body in the
# audit").
function Hands([string]$plan) {
    $dir = Join-Path $env:LOCALAPPDATA 'MEVR\audit-hands'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $file = Join-Path $dir (($plan -replace '[^A-Za-z0-9]+', '-') + '-view.mevrrec')
    if (-not (Test-Path $file)) { & python -I (Join-Path $tools 'make-walk-recording.py') $file --plan $plan --hands view | Out-Null }
    $file
}

# The run script's lines for one scene. $handsPlan '' replays no hands; $mustFind and $breaks make it a broken run.
function Scene-Lines($s, [string]$before, [string]$after, [string]$handsPlan, [string[]]$mustFind, [string]$breaks) {
    $seconds = if ($s.seconds) { [int]$s.seconds } else { 75 }
    $common = "-Pace full -Seconds $seconds -Stances -Lines 4 -Key $($s.key)"
    if ($s.endAfter) { $common += " -EndAfter $($s.endAfter)" }
    if ($s.turnAt) { $common += " -TurnAt $($s.turnAt)" }
    if ($s.wanderAt) { $common += " -WanderAt $($s.wanderAt)" }
    $common += " -Before '$(Q $before)'"
    if ($after) { $common += " -After '$(Q $after)'" }
    if ($handsPlan) { $common += " -Replay '$(Hands $handsPlan)' -HandsPlan '$(Q "$handsPlan, held in view")'" }
    if ($data.snapshotEvery) { $common += " -SnapshotEvery $($data.snapshotEvery)" }
    if ($s.expect) { $common += ' -Expect ' + ((@($s.expect) | ForEach-Object { "'$(Q $_)'" }) -join ',') }
    if ($mustFind) { $common += ' -MustFind ' + ((@($mustFind) | ForEach-Object { "'$(Q $_)'" }) -join ',') + " -Breaks '$(Q $breaks)'" }
    # The scene's record goes to the session folder ($Session is the dot-sourcing session's: tools/fake-headset-session.ps1).
    $common += ' -Report (Join-Path $Session ''audit-report.jsonl'')'
    $title = if ($breaks) { "broken run, $breaks" } else { $s.kind }
    "Say '##### $($s.key): $(Q $title)'"
    if ($s.save) {
        "& tools\conversation-from-save.ps1 -Save '$(Expand $s.save)' -Action '$($s.action)' -Class '$($s.class)' $common 2>&1"
    } else {
        $l = $data.lists.($s.from)
        $saveDir = if ($l.saveDir) { " -SaveDir '$(Expand $l.saveDir)'" } else { '' }
        "& tools\conversation-from-save.ps1 -All -Career $($l.career) -List '$(Expand $l.list)' -FirstSlot $($l.firstSlot)$saveDir -Only $($s.slot) $common 2>&1"
    }
}

function Default-Before($s) {
    # A console line before the scene (a scripted walk's player walking along: `mevr convo follow on`); every other scene starts
    # with it off.
    if ($s.before) { $s.before } else { 'mevr convo follow off' }
}

function Default-Hands($s) { if ($s.hands) { $s.hands } elseif ($data.hands) { $data.hands } else { '' } }

# Plays a run script in a session of its own; returns that session's folder.
function Play([string[]]$lines, [string]$tag) {
    $run = Join-Path $env:TEMP ("conversation-audit-{0}-{1}.ps1" -f $tag, (Get-Date -Format 'yyyyMMdd-HHmmss'))
    [IO.File]::WriteAllLines($run, $lines)
    $from = Get-Date
    $sessionArgs = @{ Tag = $tag; Run = $run }
    if ($IgnoreSlot) { $sessionArgs.IgnoreSlot = $true }
    if ($NoDeploy) { $sessionArgs.NoDeploy = $true }
    # Printed, not returned (a function's output is its return value).
    & (Join-Path $tools 'fake-headset-session.ps1') @sessionArgs 2>&1 | ForEach-Object { Write-Host $_ }
    $root = Join-Path $env:LOCALAPPDATA 'MEVR\sessions'
    Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.Name -like "*-$tag" -and $_.CreationTime -ge $from.AddSeconds(-5) } |
        Sort-Object CreationTime | Select-Object -Last 1 -ExpandProperty FullName
}

# What each scene measured and what it did not, its contact sheet, and the broken runs' verdicts (tools/audit-report.py).
function Report-Session([string]$folder) {
    if (-not $folder) { Write-Host 'no session folder found: nothing measured'; return 1 }
    & python -I (Join-Path $tools 'audit-report.py') $folder | ForEach-Object { Write-Host $_ }
    return $LASTEXITCODE
}

function Judge-Run([string]$since) {
    # Printed to the host (Write-Host, which `*>&1` and Tee-Object also catch; Out-Host bypasses every stream): a function's
    # output is its return value, and only the exit code is returned.
    $stance = Join-Path $tools 'conversation-stance.py'
    Write-Host "##### findings since $since"
    & python -I $stance --findings --since $since | ForEach-Object { Write-Host $_ }
    if ($WriteBaseline) {
        & python -I $stance --baseline-write $Baseline --since $since | ForEach-Object { Write-Host $_ }
        return 0
    }
    if (-not (Test-Path $Baseline)) {
        Write-Host "no baseline at $Baseline yet: run again with -WriteBaseline once this run is judged good"
        return 0
    }
    Write-Host "##### against the baseline ($Baseline)"
    & python -I $stance --baseline-compare $Baseline --since $since | ForEach-Object { Write-Host $_ }
    return $LASTEXITCODE
}

if ($Judge) {
    $code = Judge-Run $Judge
    if ($Session) { $code = [math]::Max($code, (Report-Session $Session)) }
    exit $code
}

$data = Get-Content -Raw $Set | ConvertFrom-Json

if ($Canaries) {
    # Broken runs (research/69 "Broken runs the audit must catch"): each in a game of its own (a council run needs a fresh game
    # process, and a test switch must not outlive its run).
    $code = 0
    foreach ($c in $data.canaries) {
        if ($Canary.Count -gt 0 -and $Canary -notcontains $c.name) { continue }
        $s = $data.scenes | Where-Object { $_.key -eq $c.key } | Select-Object -First 1
        if (-not $s) { Write-Host "broken run $($c.name): no scene $($c.key) in the set"; $code = 1; continue }
        $before = Default-Before $s
        if ($c.switch) { $before += "`n" + $c.switch }
        $handsPlan = if ($null -ne $c.hands) { $c.hands } else { Default-Hands $s }
        $lines = @('# Written by tools/conversation-audit.ps1 -Canaries (dot-sourced by tools/fake-headset-session.ps1).') +
            @(Scene-Lines $s $before $c.restore $handsPlan @($c.mustFind) $c.breaks)
        if ($Plan) { $lines; continue }
        Write-Host "##### broken run $($c.name): $($c.breaks)"
        $folder = Play $lines "canary-$($c.name)"
        $code = [math]::Max($code, (Report-Session $folder))
    }
    exit $code
}

$lines = @('# Written by tools/conversation-audit.ps1 (dot-sourced by tools/fake-headset-session.ps1).')
foreach ($s in $data.scenes) {
    if ($Only.Count -gt 0 -and $Only -notcontains $s.key) { continue }
    if ($s.long -and -not $Long) { continue }
    $lines += @(Scene-Lines $s (Default-Before $s) '' (Default-Hands $s) @() '')
}
if ($Plan) { $lines; exit 0 }
$began = Get-Date -Format 'yyyy-MM-ddTHH:mm:ss'
$folder = Play $lines 'audit'
$code = Report-Session $folder
exit ([math]::Max($code, (Judge-Run $began)))
