# SPDX-License-Identifier: MIT
# The game slot: one command to borrow the game folder for a test run and one to give it back exactly as it was
# (research/69 "The sweep: plan", phase 1). There is one game folder, shared with the release candidate and the owner's QA,
# so a session that tests in the game takes the slot, runs, and returns it; the lock file says who holds it.
#
#   tools/game-slot.ps1 status                        who holds the slot, whether the game runs
#   tools/game-slot.ps1 take -Holder sweep            back up, deploy this checkout's build, fake headset on
#   tools/game-slot.ps1 take -Holder sweep -Set 'Cinematics.conversations=world','Cinematics.worldAutoScreen=log'
#                                                     ... and those keys written into the deployed LE1MEVR.ini
#   tools/game-slot.ps1 take -Holder sweep -NoDeploy -NoHeadset     back up and lock only
#   tools/game-slot.ps1 return [-Quit]                fake headset off, everything put back, every hash checked, lock removed
#
# take refuses while the game runs or the slot is held. It copies the ASI folder, the top-level files of Binaries\Win64
# (not the exe: hashed only), BioGame\Config and the ME1 saves to %LOCALAPPDATA%\MEVR\game-slot\backups\<stamp>, with the
# SHA-256 of every file (hashes.json), writes lock.json, then deploys (tools/deploy-asi.ps1 -IgnoreSlot) and turns the fake
# headset on (tools/fake-headset.ps1 -On). return puts back every file that is missing or differs, moves files that were
# not there before into the backup's `extras` folder (the run's saves, logs and the hot-reload path: never deleted), hashes
# everything again and fails loudly on any difference; the ASI's SHA-256 before and after is printed (the release
# candidate's is the one to compare). The newest three backups are kept.
#
# The lock is a convention other tools read: tools/deploy-asi.ps1 refuses to deploy over a held slot. Tell the session that
# owns the game folder's build when you take it and when you return it.
param(
    [Parameter(Position = 0)] [ValidateSet('status', 'take', 'return')] [string]$Action = 'status',
    [string]$Holder = '',
    [string[]]$Set = @(),
    [switch]$NoDeploy,
    [switch]$NoHeadset,
    [switch]$Quit,
    [string]$BuildDir = '',
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition',
    [string]$SaveRoot = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition\Save\ME1'),
    [string]$StateDir = (Join-Path $env:LOCALAPPDATA 'MEVR\game-slot'),
    [int]$KeepBackups = 3
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
# ($PSScriptRoot is empty in the param block of a script with [Parameter()] attributes on Windows PowerShell 5.1.)
if (-not $BuildDir) { $BuildDir = Join-Path $tools '..\build' }
$win64 = Join-Path $GameRoot 'Game\ME1\Binaries\Win64'
$lockFile = Join-Path $StateDir 'lock.json'
# Each place: its folder, whether its subfolders count, and the files hashed but not copied.
$places = @(
    [pscustomobject]@{ name = 'asi'; root = (Join-Path $win64 'ASI'); deep = $true; hashOnly = @() },
    [pscustomobject]@{ name = 'win64'; root = $win64; deep = $false; hashOnly = @('MassEffect1.exe') },
    [pscustomobject]@{ name = 'config'; root = (Join-Path $GameRoot 'Game\ME1\BioGame\Config'); deep = $true; hashOnly = @() },
    [pscustomobject]@{ name = 'saves'; root = $SaveRoot; deep = $true; hashOnly = @() }
)

function Running() { [bool](Get-Process MassEffect1 -ErrorAction SilentlyContinue) }
function Files($place) {
    if (-not (Test-Path -LiteralPath $place.root)) { return @() }
    if ($place.deep) { return @(Get-ChildItem -LiteralPath $place.root -File -Recurse -Force) }
    return @(Get-ChildItem -LiteralPath $place.root -File -Force)
}
function Rel($place, $file) { $file.FullName.Substring($place.root.Length).TrimStart([char]92) }
function Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
function Hashes($place) {
    $map = [ordered]@{}
    foreach ($f in (Files $place | Sort-Object FullName)) { $map[(Rel $place $f)] = Sha $f.FullName }
    return $map
}
function ReadLock() { if (Test-Path -LiteralPath $lockFile) { Get-Content -LiteralPath $lockFile -Raw | ConvertFrom-Json } else { $null } }
function AsiSha() { $p = Join-Path $win64 'ASI\LE1MEVR.asi'; if (Test-Path -LiteralPath $p) { Sha $p } else { '' } }
function WriteIni([string]$file, [string]$section, [string]$key, [string]$value) {
    if (-not ('MevrSlot.Ini' -as [type])) {
        Add-Type -Namespace MevrSlot -Name Ini -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern bool WritePrivateProfileStringW(string section, string key, string value, string file);'
    }
    return [MevrSlot.Ini]::WritePrivateProfileStringW($section, $key, $value, $file)
}

if ($Action -eq 'status') {
    $lock = ReadLock
    if ($lock) { Write-Output ("held by '{0}' since {1}; backup {2}; the ASI then: {3}" -f $lock.holder, $lock.since, $lock.backup, $lock.asiSha256) }
    else { Write-Output 'free' }
    Write-Output ("the game is {0}; the ASI now: {1}" -f $(if (Running) { 'running' } else { 'not running' }), (AsiSha))
    return
}

if ($Action -eq 'take') {
    if (-not $Holder) { throw 'take needs -Holder <who> (a session or a task name others will recognise)' }
    $lock = ReadLock
    if ($lock) { throw "the game folder is held by '$($lock.holder)' since $($lock.since): tools/game-slot.ps1 return gives it back" }
    if (Running) { throw 'the game is running: the folder is in use (close it, or wait)' }
    foreach ($pair in $Set) { if ($pair -notmatch '^[^.=]+\.[^=]+=') { throw "-Set takes Section.key=value, not '$pair'" } }
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $backup = Join-Path $StateDir "backups\$stamp"
    New-Item -ItemType Directory -Force $backup | Out-Null
    $all = [ordered]@{}
    $count = 0
    foreach ($place in $places) {
        $dst = Join-Path $backup $place.name
        New-Item -ItemType Directory -Force $dst | Out-Null
        foreach ($f in (Files $place)) {
            $rel = Rel $place $f
            if ($place.hashOnly -contains $rel) { continue }
            $to = Join-Path $dst $rel
            New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $to -Force
        }
        $all[$place.name] = Hashes $place
        $count += $all[$place.name].Count
    }
    # The copies must be what was hashed: one pass over the backup before anything in the game folder changes.
    foreach ($place in $places) {
        foreach ($rel in $all[$place.name].Keys) {
            if ($place.hashOnly -contains $rel) { continue }
            if ((Sha (Join-Path (Join-Path $backup $place.name) $rel)) -ne $all[$place.name][$rel]) { throw "the backup of $($place.name)\$rel differs from the file: nothing taken" }
        }
    }
    ($all | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath (Join-Path $backup 'hashes.json') -Encoding utf8
    $lock = [ordered]@{ holder = $Holder; since = (Get-Date -Format 's'); backup = $backup; asiSha256 = (AsiSha); headset = $false; deployed = $false; files = $count }
    New-Item -ItemType Directory -Force $StateDir | Out-Null
    ($lock | ConvertTo-Json) | Set-Content -LiteralPath $lockFile -Encoding utf8
    Write-Output "taken by '$Holder': $count files backed up and hashed in $backup"
    Write-Output "the ASI before: $($lock.asiSha256)"
    if (-not $NoDeploy) {
        & (Join-Path $tools 'deploy-asi.ps1') -Game LE1 -BuildDir $BuildDir -IgnoreSlot
        $lock.deployed = $true
        Write-Output "the ASI now:    $(AsiSha)"
    }
    $ini = Join-Path $win64 'ASI\LE1MEVR.ini'
    foreach ($pair in $Set) {
        $m = [regex]::Match($pair, '^([^.=]+)\.([^=]+)=(.*)$')
        if (-not (WriteIni $ini $m.Groups[1].Value $m.Groups[2].Value $m.Groups[3].Value)) { throw "couldn't write $pair into $ini" }
        Write-Output "   [$($m.Groups[1].Value)] $($m.Groups[2].Value) = $($m.Groups[3].Value)"
    }
    if (-not $NoHeadset) {
        & (Join-Path $tools 'fake-headset.ps1') -On
        $lock.headset = $true
    }
    ($lock | ConvertTo-Json) | Set-Content -LiteralPath $lockFile -Encoding utf8
    # Old backups: the newest $KeepBackups stay.
    $old = @(Get-ChildItem -LiteralPath (Join-Path $StateDir 'backups') -Directory | Sort-Object Name -Descending | Select-Object -Skip $KeepBackups)
    foreach ($d in $old) { Remove-Item -LiteralPath $d.FullName -Recurse -Force -Confirm:$false; Write-Output "   old backup $($d.Name) removed" }
    return
}

# return
$lock = ReadLock
if (-not $lock) { throw 'the slot is free: nothing to return' }
if (Running) {
    if (-not $Quit) { throw 'the game is running: close it first (or -Quit)' }
    try { & (Join-Path $tools 'launch-game.ps1') -Quit | Out-Null } catch { Write-Warning "the quit through the console failed: $_" }
    $deadline = (Get-Date).AddSeconds(60)
    while ((Running) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    if (Running) { throw 'the game is still running 60 s after the quit: nothing returned' }
    Start-Sleep -Seconds 3
}
if ($lock.headset) { & (Join-Path $tools 'fake-headset.ps1') -Off }
$backup = $lock.backup
$before = Get-Content -LiteralPath (Join-Path $backup 'hashes.json') -Raw | ConvertFrom-Json
$putBack = 0; $moved = 0; $bad = @()
foreach ($place in $places) {
    $want = @{}
    foreach ($p in $before.($place.name).PSObject.Properties) { $want[$p.Name] = $p.Value }
    foreach ($f in (Files $place)) {
        $rel = Rel $place $f
        if ($want.ContainsKey($rel)) { continue }
        $to = Join-Path (Join-Path $backup "extras\$($place.name)") $rel
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Move-Item -LiteralPath $f.FullName -Destination $to -Force
        $moved++
    }
    foreach ($rel in $want.Keys) {
        $at = Join-Path $place.root $rel
        if ((Test-Path -LiteralPath $at) -and (Sha $at) -eq $want[$rel]) { continue }
        if ($place.hashOnly -contains $rel) { $bad += "$($place.name)\$rel changed and has no copy"; continue }
        New-Item -ItemType Directory -Force (Split-Path $at) | Out-Null
        Copy-Item -LiteralPath (Join-Path (Join-Path $backup $place.name) $rel) -Destination $at -Force
        $putBack++
    }
    $now = Hashes $place
    foreach ($rel in $want.Keys) { if ($now[$rel] -ne $want[$rel]) { $bad += "$($place.name)\$rel is not as before" } }
    foreach ($rel in $now.Keys) { if (-not $want.ContainsKey($rel)) { $bad += "$($place.name)\$rel is new" } }
}
$asiNow = AsiSha
Write-Output "returned by '$($lock.holder)': $putBack files put back, $moved new files moved to $backup\extras"
Write-Output "the ASI before: $($lock.asiSha256)"
Write-Output "the ASI now:    $asiNow"
if ($bad.Count -gt 0 -or $asiNow -ne $lock.asiSha256) {
    $bad | ForEach-Object { Write-Warning $_ }
    throw "the game folder is NOT as it was ($($bad.Count) differences): the lock stays, the backup is $backup"
}
Remove-Item -LiteralPath $lockFile -Confirm:$false
Write-Output "every one of the $($lock.files) files matches its hash from before; the slot is free"
