# SPDX-License-Identifier: MIT
# One game run at a time on this machine, across parallel sessions and other agents: a lock file that names who runs
# the game. prep.ps1 takes it for a stretch of ME2/ME3 runs and restore.ps1 gives it back; take it by hand around any other
# game run (an LE1 run with tools/fake-headset-session.ps1, a measurement), so two runners never start the game, deploy
# into its folder or restore its saves under each other.
# It is %LOCALAPPDATA%\MEVR\game-run.lock, the convention tools/qa-le2 and tools/qa-cafeteria already follow: their runners
# refuse while any *.lock is in that folder (they hold qa-cafeteria.lock while they run), and this one refuses while any
# lock of theirs, or the game slot (tools/game-slot.ps1), is held.
#   run-lock.ps1 -Status
#   run-lock.ps1 -Take -Holder <session name> [-Note <what for>]    (fails while anyone else holds a lock)
#   run-lock.ps1 -Release -Holder <session name>
param(
    [switch]$Take,
    [switch]$Release,
    [switch]$Status,
    [string]$Holder,
    [string]$Note = ''
)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $env:LOCALAPPDATA 'MEVR'
$lock = Join-Path $dir 'game-run.lock'
$slot = Join-Path $dir 'game-slot\lock.json'

function Read-Lock {
    if (-not (Test-Path -LiteralPath $lock)) { return $null }
    return Get-Content -LiteralPath $lock -Raw | ConvertFrom-Json
}
function Other-Locks {
    @(Get-ChildItem -LiteralPath $dir -Filter '*.lock' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'game-run.lock' })
}

$held = Read-Lock
if ($Status -or -not ($Take -or $Release)) {
    if ($held) { "held by '$($held.holder)' since $($held.since): $($held.note)" } else { 'game-run.lock: free' }
    foreach ($o in Other-Locks) { "also held: $($o.Name) since $($o.LastWriteTime.ToString('s')) ($((Get-Content -LiteralPath $o.FullName -Raw -ErrorAction SilentlyContinue)))" }
    if (Test-Path -LiteralPath $slot) { 'also held: the game slot (tools/game-slot.ps1 status)' }
    $running = Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue
    if ($running) { 'running now: ' + (($running | ForEach-Object { "$($_.ProcessName) ($($_.Id))" }) -join ', ') }
    exit 0
}
if (-not $Holder) { throw '-Holder is required' }

if ($Take) {
    if ($held -and $held.holder -ne $Holder) {
        throw "the game is held by '$($held.holder)' since $($held.since) ($($held.note)): wait for its release"
    }
    if (-not $held) {
        $others = Other-Locks
        if ($others) { throw "another runner holds $($others[0].Name) (tools/qa-le2 or qa-cafeteria): wait for its release" }
        if (Test-Path -LiteralPath $slot) { throw 'the game slot is held (tools/game-slot.ps1 status): not ours to take' }
        if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) {
            throw 'a game or its launcher runs and nobody holds a lock: someone else (the owner, another agent) is using it'
        }
    }
    New-Item -ItemType Directory -Force $dir | Out-Null
    if (-not $held) {
        # The existence check above is advisory. CreateNew is the atomic ownership decision.
        $stream = [IO.File]::Open($lock, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        try {
            $json = [pscustomobject]@{ holder = $Holder; since = (Get-Date -Format 's'); note = $Note } | ConvertTo-Json
            $bytes = [Text.Encoding]::UTF8.GetBytes($json)
            $stream.Write($bytes, 0, $bytes.Length)
            $stream.Flush()
        } finally { $stream.Dispose() }
    } else {
        # Do not replace a lock which changed while we checked it, or reset its original timestamp.
        $again = Read-Lock
        if (-not $again -or $again.holder -ne $Holder -or $again.since -ne $held.since) { throw 'Run lock changed during acquisition' }
    }
    "taken by '$Holder'"
    exit 0
}

if ($Release) {
    if (-not $held) { 'already free'; exit 0 }
    if ($held.holder -ne $Holder) { throw "held by '$($held.holder)', not '$Holder': not released" }
    Remove-Item -LiteralPath $lock -Confirm:$false
    "released by '$Holder'"
}
