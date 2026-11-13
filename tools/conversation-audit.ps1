# SPDX-License-Identifier: MIT
# The regression set of first-person scenes, played and judged (research/69 "Auditing scenes by rule"; the skill
# `first-person-scene-audit`): every scene of tools/conversation-audit-set.json from its save, with the overhead records on and a
# record at each of the first lines, through tools/fake-headset-session.ps1 (deploy, back up, fake headset, launch, restore);
# then the rules' findings (tools/scene_audit.py) and the comparison with the baseline. About 12 minutes, 15 with -Long.
#
#   tools/conversation-audit.ps1 [-Long] [-Only sta20_chorban_dlg,ice20_opold_dlg] [-IgnoreSlot] [-NoDeploy]
#   tools/conversation-audit.ps1 -WriteBaseline      the run's numbers become the baseline (after a change judged good)
#   tools/conversation-audit.ps1 -Judge <yyyy-MM-ddTHH:mm:ss>   judge an earlier run's records again, no game
#   tools/conversation-audit.ps1 -Plan [-Long]        print the run script it would play, no game
#
# Exit code 1 when the comparison finds a regression. The game must be free (another session's run would be disturbed).
param(
    [string]$Set = '',
    [string]$Baseline = '',
    [switch]$WriteBaseline,
    [switch]$Long,
    [string[]]$Only = @(),
    [switch]$IgnoreSlot,
    [switch]$NoDeploy,
    [string]$Judge = '',
    [switch]$Plan
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
if (-not $Set) { $Set = Join-Path $tools 'conversation-audit-set.json' }
if (-not $Baseline) { $Baseline = Join-Path $tools 'conversation-audit-baseline.json' }

function Expand([string]$path) { [Environment]::ExpandEnvironmentVariables($path) }

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

if ($Judge) { exit (Judge-Run $Judge) }

$data = Get-Content -Raw $Set | ConvertFrom-Json
$lines = @('# Written by tools/conversation-audit.ps1 (dot-sourced by tools/fake-headset-session.ps1).')
foreach ($s in $data.scenes) {
    if ($Only.Count -gt 0 -and $Only -notcontains $s.key) { continue }
    if ($s.long -and -not $Long) { continue }
    $seconds = if ($s.seconds) { [int]$s.seconds } else { 75 }
    $common = "-Pace full -Seconds $seconds -Stances -Lines 4"
    if ($s.endAfter) { $common += " -EndAfter $($s.endAfter)" }
    if ($s.turnAt) { $common += " -TurnAt $($s.turnAt)" }
    if ($s.wanderAt) { $common += " -WanderAt $($s.wanderAt)" }
    # A console line before the scene (a scripted walk's player walking along: `mevr convo follow on`); every other scene
    # starts with it off.
    $before = if ($s.before) { $s.before } else { 'mevr convo follow off' }
    $common += " -Before '$($before -replace "'", "''")'"
    $lines += "Say '##### $($s.key): $($s.kind -replace "'", "''")'"
    if ($s.save) {
        $lines += "& tools\conversation-from-save.ps1 -Save '$(Expand $s.save)' -Action '$($s.action)' -Class '$($s.class)' $common 2>&1"
    } else {
        $l = $data.lists.($s.from)
        $saveDir = if ($l.saveDir) { " -SaveDir '$(Expand $l.saveDir)'" } else { '' }
        $lines += "& tools\conversation-from-save.ps1 -All -Career $($l.career) -List '$(Expand $l.list)' -FirstSlot $($l.firstSlot)$saveDir -Only $($s.slot) $common 2>&1"
    }
}
if ($Plan) { $lines; exit 0 }
$run = Join-Path $env:TEMP ("conversation-audit-{0}.ps1" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.File]::WriteAllLines($run, $lines)
$began = Get-Date -Format 'yyyy-MM-ddTHH:mm:ss'
$sessionArgs = @{ Tag = 'audit'; Run = $run }
if ($IgnoreSlot) { $sessionArgs.IgnoreSlot = $true }
if ($NoDeploy) { $sessionArgs.NoDeploy = $true }
& (Join-Path $tools 'fake-headset-session.ps1') @sessionArgs
exit (Judge-Run $began)
