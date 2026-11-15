# SPDX-License-Identifier: MIT
# Only a prepared, locked session may launch or stop its own recorded processes.
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [switch]$DesktopDump,
    [switch]$EngineProbe,
    [switch]$EngineRequests,
    [switch]$Kill,
    [switch]$Stop
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\session-common.ps1"
Assert-RunMode $Game ([bool]$DesktopDump) ([bool]$EngineProbe) ([bool]$EngineRequests)
$open = Read-RunSession $Holder $Game ([bool]$DesktopDump) ([bool]$EngineProbe) ([bool]$EngineRequests)
. "$PSScriptRoot\..\common.ps1"
$names = Get-RunNames $Game
$install = (Find-MELEInstall).Root
if ($open.install -and $open.install -ne $install) { throw 'Install changed since backup' }
$bin = Get-MELEBinDir $install $Game
$exe = Join-Path $bin "$($names.process).exe"
$launcherExe = Join-Path $install 'Game\Launcher\MassEffectLauncher.exe'
function Stop-Owned {
    $gameProcess = @(Get-Process -Name $names.process -ErrorAction SilentlyContinue)
    $launcherProcess = @(Get-Process MassEffectLauncher -ErrorAction SilentlyContinue)
    # Validate every target before stopping any. Old metadata without a PID is not ownership.
    foreach ($p in $gameProcess) { Assert-RunProcess $p $open.gameProcess $exe }
    foreach ($p in $launcherProcess) { Assert-RunProcess $p $open.launcherProcess $launcherExe }
    foreach ($p in $gameProcess) {
        $still = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
        if ($still) { Assert-RunProcess $still $open.gameProcess $exe; Stop-Process -Id $p.Id -Force -Confirm:$false }
    }
    foreach ($p in $launcherProcess) {
        $still = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
        if ($still) { Assert-RunProcess $still $open.launcherProcess $launcherExe; Stop-Process -Id $p.Id -Confirm:$false }
    }
}
$others = @(Get-Process MassEffect1, MassEffect2, MassEffect3 -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -ne $names.process })
if ($others.Count) { throw 'Another game is running: refusing this operation' }
if ($Stop -or $Kill) { Stop-Owned }
if ($Stop) { "$Game owned processes closed"; exit 0 }
if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) { throw 'A game or launcher is already running' }
if ($DesktopDump) {
    Assert-RunDumpHost "$bin\ASI\${Game}MEVR.asi" ([bool]$EngineProbe)
    $config = [IO.File]::ReadAllText("$bin\ASI\${Game}MEVR.ini")
    # Prep writes this minimal file. Refuse modifications/duplicates that could select a different mode.
    Assert-RunDesktopConfig $config ([bool]$EngineProbe) ([bool]$EngineRequests)
}
if (-not $DesktopDump) {
    $vr = $null
    for ($i = 0; $i -lt 30 -and -not $vr; $i++) { $vr = Get-Process vrserver -ErrorAction SilentlyContinue; if (-not $vr) { Start-Sleep 1 } }
    if (-not $vr) { throw 'SteamVR is not running (LE2 prep turns the fake headset on)' }
    $wait = 16 - ((Get-Date) - $vr.StartTime).TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Seconds ([int][math]::Ceiling($wait)) }
}
$log = if ($DesktopDump) { Join-Path $bin "Logs\${Game}MEVR.log" } else { Join-Path $env:LOCALAPPDATA 'MELE2VR\MELE2VR_Log.txt' }
$oldLog = if (Test-Path -LiteralPath $log) { Read-RunLog $log } else { '' }
$started = [DateTime]::UtcNow
$open | Add-Member -NotePropertyName launchRequestedUtc -NotePropertyValue $started.ToString('o') -Force
$open | Add-Member -NotePropertyName gameProcess -NotePropertyValue $null -Force
$open | Add-Member -NotePropertyName launcherProcess -NotePropertyValue $null -Force
Write-RunSession $open $names.current
Start-Process -WindowStyle Hidden -FilePath 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList '-applaunch', '1328670', '-game', $names.number, '-autoterminate'
"Asked Steam for $Game at $(Get-Date -Format HH:mm:ss)"
$p = $null
for ($i = 0; $i -lt 120 -and -not $p; $i++) {
    $launchers = @(Get-Process MassEffectLauncher -ErrorAction SilentlyContinue)
    if ($launchers.Count -gt 1) { throw 'Multiple launchers appeared; refusing to adopt them' }
    foreach ($launcher in $launchers) {
        $identity = Get-RunLaunchIdentity $launcher.Id $launcherExe $started
        if ($identity) {
            $open.launcherProcess = $identity
            Write-RunSession $open $names.current
        }
    }
    $found = @(Get-Process -Name $names.process -ErrorAction SilentlyContinue)
    if ($found.Count -gt 1) { throw 'Multiple game processes appeared; refusing to adopt them' }
    if ($found.Count) {
        $p = Get-RunLaunchIdentity $found[0].Id $exe $started
        if ($p) {
            $open.gameProcess = $p
            Write-RunSession $open $names.current
        }
    }
    if (-not $p) { Start-Sleep 2 }
}
if (-not $p) { throw "No $($names.process) within 240 seconds; keep the session locked and inspect it" }
"$($names.process) pid $($p.pid) startedUtc $($p.startedUtc)"
$marker = if ($EngineProbe) { 'engine-probe: reflection checks finished' } elseif ($DesktopDump) { 'dump-only: code dump attempted; renderer and hooks disabled' } else { 'VR session ready' }
$seen = $false
for ($i = 0; $i -lt 40; $i++) {
    if (Test-Path -LiteralPath $log) {
        $text = Read-RunLog $log
        $fresh = if ($oldLog -and $text.StartsWith($oldLog, [StringComparison]::Ordinal)) { $text.Substring($oldLog.Length) } else { $text }
        if ((Get-Item -LiteralPath $log).LastWriteTimeUtc -ge $started -and $fresh.Contains($marker)) { $seen = $true; break }
    }
    if (-not (Get-Process -Id $p.pid -ErrorAction SilentlyContinue)) { throw 'The game exited before the expected host marker' }
    Start-Sleep 2
}
if (-not $seen) { throw "Expected marker was not observed: $marker (the session remains owned; use -Stop, then restore)" }
$marker
if ($DesktopDump) {
    if ($EngineProbe) {
        $reflection = Confirm-RunReflection (Join-Path $env:LOCALAPPDATA 'MEVR\reflect-check.json') $started
        Copy-Item -LiteralPath $reflection.path -Destination (Join-Path $open.session 'reflect-check.json')
    }
    $dump = Confirm-RunDump $exe (Join-Path $env:LOCALAPPDATA 'MEVR\dump')
    $receipt = [pscustomobject]@{ game = $Game; desktopDump = $true; holder = $Holder; launchRequestedUtc = $started.ToString('o');
        process = $open.gameProcess; marker = $marker; dump = $dump; hostSha256 = (Get-FileHash -LiteralPath "$bin\ASI\${Game}MEVR.asi").Hash;
        freshDump = ([DateTime]::Parse($dump.writtenUtc).ToUniversalTime() -ge $started); nativeAcceptance = $false;
        engineProbe = [bool]$EngineProbe; engineRequests = [bool]$EngineRequests; reflection = $(if ($EngineProbe) { $reflection } else { $null }) }
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $open.session 'desktop-dump.json') -Encoding utf8
    "Dump headers, size and hashes checked; fresh dump this launch: $($receipt.freshDump). Receipt: $($open.session)\desktop-dump.json"
}
