# SPDX-License-Identifier: MIT
# Only a prepared, locked session may launch or stop its own recorded processes.
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [switch]$DesktopDump,
    [switch]$EngineProbe,
    [switch]$EngineRequests,
    [switch]$RendererPreview,
    [ValidateRange(10, 600)][int]$LaunchTimeoutSeconds = 240,
    [ValidateRange(10, 300)][int]$ReadyTimeoutSeconds = 80,
    [switch]$Kill,
    [switch]$Stop
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\session-common.ps1"
Assert-RunMode $Game ([bool]$DesktopDump) ([bool]$EngineProbe) ([bool]$EngineRequests) ([bool]$RendererPreview)
$open = Read-RunSession $Holder $Game ([bool]$DesktopDump) ([bool]$EngineProbe) ([bool]$EngineRequests) ([bool]$RendererPreview)
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
if ($RendererPreview) {
    $previewOptions = Get-PreviewOptions $open
    Assert-PreviewConfig $bin $previewOptions.mode $previewOptions.diagnostics $previewOptions.mirrorEvery
    Assert-RunArtifacts $open.previewArtifacts
    Assert-PreviewEnvironment $open (Get-PreviewPaths)
    if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'MEVR\fake-headset\undo.json')) { throw 'Another fake-headset session appeared' }
    $open | Add-Member -NotePropertyName previewProcesses -NotePropertyValue @(Get-PreviewVrProcesses $open $true) -Force
    if (-not @($open.previewProcesses | Where-Object { $_.name -eq 'vrserver' }).Count) { throw 'No owned preview VR server' }
    Write-RunSession $open $names.current
    Backup-RunConsole $open $names.current (Join-Path $env:LOCALAPPDATA 'MEVR')
    Backup-RunConsole $open $names.current (Join-Path $env:LOCALAPPDATA 'MEVR') -Names (Get-PreviewChannels) -Metadata 'previewChannelsBefore' -Directory 'preview-channels-before'
}
if (-not $DesktopDump) {
    $vr = $null
    for ($i = 0; $i -lt 30 -and -not $vr; $i++) { $vr = Get-Process vrserver -ErrorAction SilentlyContinue; if (-not $vr) { Start-Sleep 1 } }
    if (-not $vr) { throw 'SteamVR is not running (VR prep turns the owned fake headset on)' }
    $wait = 16 - ((Get-Date) - $vr.StartTime).TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Seconds ([int][math]::Ceiling($wait)) }
}
$log = if ($DesktopDump) { Join-Path $bin "Logs\${Game}MEVR.log" } else { Get-RunRendererLog $Game }
$logs = @($log)
if ($RendererPreview) {
    $logs += Join-Path $bin 'ME3PassiveDxgiProbe.log' # actual logger fallback
    $settingsLogs = @((Join-Path $bin 'Logs\LE3MEVR.log'), (Join-Path $env:LOCALAPPDATA 'MEVR\LE3MEVR.log'))
    $logs += $settingsLogs
}
$oldLogs = @{}
foreach ($path in $logs) { $oldLogs[$path] = if (Test-Path -LiteralPath $path) { Read-RunLog $path } else { '' } }
$started = [DateTime]::UtcNow
$open | Add-Member -NotePropertyName launchRequestedUtc -NotePropertyValue $started.ToString('o') -Force
$open | Add-Member -NotePropertyName gameProcess -NotePropertyValue $null -Force
$open | Add-Member -NotePropertyName launcherProcess -NotePropertyValue $null -Force
Write-RunSession $open $names.current
$previewReceipt = if ($RendererPreview) { [ordered]@{ schemaVersion = 1; game = 'LE3'; rendererPreview = $true;
    holder = $Holder; session = $open.session; launchRequestedUtc = $started.ToString('o'); artifactsBefore = $open.previewArtifacts;
    rendererOptions = $previewOptions;
    marker = '[ME3XR] VR session ready; starting frame submission.'; freshMarker = $false; stereoAccepted = $false;
    physicalHeadsetAccepted = $false; error = $null } } else { $null }
try {
Assert-RunLock $Holder
if ($RendererPreview) {
    $open.previewProcesses = @(Get-PreviewVrProcesses $open $true)
    if (-not @($open.previewProcesses | Where-Object { $_.name -eq 'vrserver' }).Count) { throw 'Owned preview VR server exited before launch' }
    Write-RunSession $open $names.current
}
Start-Process -WindowStyle Hidden -FilePath 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList '-applaunch', '1328670', '-game', $names.number, '-autoterminate'
"Asked Steam for $Game at $(Get-Date -Format HH:mm:ss)"
$p = $null
for ($i = 0; $i -lt [math]::Ceiling($LaunchTimeoutSeconds / 2) -and -not $p; $i++) {
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
if (-not $p) { throw "No $($names.process) within $LaunchTimeoutSeconds seconds; keep the session locked and inspect it" }
"$($names.process) pid $($p.pid) startedUtc $($p.startedUtc)"
$marker = if ($EngineProbe) { 'engine-probe: reflection checks finished' } elseif ($DesktopDump) { 'dump-only: code dump attempted; renderer and hooks disabled' } elseif ($RendererPreview) { '[ME3XR] VR session ready; starting frame submission.' } else { 'VR session ready' }
$seen = $false
for ($i = 0; $i -lt [math]::Ceiling($ReadyTimeoutSeconds / 2); $i++) {
    Assert-RunLock $Holder
    $live = Get-Process -Id $p.pid -ErrorAction SilentlyContinue
    if (-not $live) { throw 'The game exited before the expected host marker' }
    Assert-RunProcess $live $open.gameProcess $exe
    foreach ($path in $logs) {
        if (Test-RunFreshMarker $path $oldLogs[$path] $started $marker) { $seen = $true; $log = $path; break }
    }
    if ($seen) { break }
    Start-Sleep 2
}
if (-not $seen) { throw "Expected marker was not observed: $marker (the session remains owned; use -Stop, then restore)" }
$marker
if ($RendererPreview) {
    $previewReceipt.freshMarker = $true; $previewReceipt.log = $log
    # The renderer can migrate an obsolete INI mode while starting. Never label
    # that run with the requested mode or continue on changed runtime artifacts.
    $initialized = Wait-PreviewInitializedConfig -Bin $bin -Artifacts $open.previewArtifacts -StartupLogs $settingsLogs `
        -Before $oldLogs -Started $started -Mode $previewOptions.mode -Diagnostics $previewOptions.diagnostics `
        -MirrorEvery $previewOptions.mirrorEvery -Guard {
            Assert-RunLock $Holder
            $live = Get-Process -Id $p.pid -ErrorAction Stop
            Assert-RunProcess $live $open.gameProcess $exe
        }
    [IO.File]::WriteAllText((Join-Path $open.session 'effective-preview-mod.ini'), $initialized.text)
    $previewReceipt.rendererConfigurationUnchanged = $true
    $previewReceipt.modDefaultsAppended = $initialized.defaultsAppended
    $previewReceipt.settingsInitializationMarkersFresh = $initialized.initializationMarkersFresh
    $previewReceipt.settingsInitializationLog = $initialized.initializationLog
    $previewReceipt.settingsStableMilliseconds = $initialized.stableMilliseconds
    $previewReceipt.configurationUnchanged = -not $initialized.defaultsAppended
}
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

} catch {
    if ($previewReceipt) { $previewReceipt.error = $_.Exception.Message }
    throw
} finally {
    if ($previewReceipt) {
        $previewReceipt.finishedUtc = [DateTime]::UtcNow.ToString('o')
        $previewReceipt.process = $open.gameProcess
        $previewReceipt.launcherProcess = $open.launcherProcess
        try {
            $owned = Get-Process -Id $open.gameProcess.pid -ErrorAction Stop
            Assert-RunProcess $owned $open.gameProcess $exe
            $previewReceipt.loadedModules = @($owned.Modules | Where-Object { $_.ModuleName -match '^LE3MEVR(?:_mod)?\.(asi|dll)$' } |
                ForEach-Object { [pscustomobject]@{ path = $_.FileName; sha256 = (Get-FileHash -LiteralPath $_.FileName).Hash } })
        } catch { $previewReceipt.loadedModuleReadError = $_.Exception.Message }
        try { $previewReceipt.artifactsAfter = @(Get-PreviewArtifacts $bin) }
        catch { $previewReceipt.artifactReadError = $_.Exception.Message }
        $previewReceipt | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $open.session 'renderer-preview-launch.json') -Encoding utf8
        'Renderer-preview receipt saved; an XR readiness marker is not stereo, gameplay, or headset acceptance'
    }
}
