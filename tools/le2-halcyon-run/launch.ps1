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
        if ($still) {
            Assert-RunProcess $still $open.gameProcess $exe
            Stop-Process -Id $p.Id -Force -Confirm:$false
            if (-not $still.WaitForExit(15000)) { throw 'Owned game did not exit within 15 seconds; keep the session locked' }
        }
    }
    foreach ($p in $launcherProcess) {
        $still = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
        if ($still) {
            Assert-RunProcess $still $open.launcherProcess $launcherExe
            Stop-Process -Id $p.Id -Confirm:$false
            if (-not $still.WaitForExit(15000)) { throw 'Owned launcher did not exit within 15 seconds; keep the session locked' }
        }
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
    Assert-PreviewConfig $bin $previewOptions.mode $previewOptions.diagnostics $previewOptions.mirrorEvery $previewOptions.eyeFrustum $previewOptions.renderSize
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
# ME2's VR mode: the mod's first-run settings appends are waited out after the XR marker (Wait-RunModSettings).
# Kept apart from $logs, which the readiness loop searches for the renderer's marker.
$modLogs = if (-not $DesktopDump -and -not $RendererPreview) { @(Get-RunModLogs $bin $Game) } else { @() }
$oldLogs = @{}
foreach ($path in @($logs + $modLogs)) { $oldLogs[$path] = if (Test-Path -LiteralPath $path) { Read-RunLog $path } else { '' } }
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
# EA's Steam launcher holds MassEffectLauncher suspended for up to ~14 s (no readable Path), so every candidate is
# tracked over time (session-common.ps1, Step-RunAdoption): the launcher is recorded once readable, the game only
# after its identity has stayed readable and unchanged for the settle time. Each candidate and verdict is printed
# and kept in the session's launch-candidates.txt.
$launcherAdoption = New-RunAdoption 'MassEffectLauncher' $launcherExe $started
$gameAdoption = New-RunAdoption $names.process $exe $started -SettleSeconds 5
$foreign = @(Get-RunForeignProcesses $names.current)
$candidateLog = Join-Path $open.session 'launch-candidates.txt'
Add-Content -LiteralPath $candidateLog -Value "== $Game launch requested $($started.ToString('o')) by $Holder" -Encoding utf8
function Read-LaunchCandidates([string]$Name, $Adoption) {
    foreach ($candidate in @(Get-Process -Name $Name -ErrorAction SilentlyContinue)) {
        $o = Read-RunCandidate $candidate.Id
        if ($o.alive -and -not $Adoption.candidates.ContainsKey([string]$candidate.Id)) {
            $o | Add-Member -NotePropertyName parent -NotePropertyValue (Get-RunParentText $candidate.Id)
        }
        $o
    }
}
function Write-LaunchCandidateLog {
    $lines = @(Get-RunAdoptionLines $launcherAdoption) + @(Get-RunAdoptionLines $gameAdoption)
    if ($lines.Count) { Add-Content -LiteralPath $candidateLog -Value $lines -Encoding utf8; $lines }
}
$p = $null
$deadline = $started.AddSeconds($LaunchTimeoutSeconds)
try {
    while (-not $p) {
        Assert-RunLock $Holder
        if (@(Get-Process MassEffect1, MassEffect2, MassEffect3 -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -ne $names.process }).Count) {
            throw 'Another game started during the launch; refusing to adopt'
        }
        $now = [DateTime]::UtcNow
        $launcher = Step-RunAdoption $launcherAdoption @(Read-LaunchCandidates 'MassEffectLauncher' $launcherAdoption) $now $foreign
        if ($launcher -and (-not $open.launcherProcess -or $open.launcherProcess.pid -ne $launcher.pid -or $open.launcherProcess.startedUtc -ne $launcher.startedUtc)) {
            $open.launcherProcess = $launcher
            Write-RunSession $open $names.current
        }
        $p = Step-RunAdoption $gameAdoption @(Read-LaunchCandidates $names.process $gameAdoption) $now $foreign
        if ($p) {
            $open.gameProcess = $p
            Write-RunSession $open $names.current
        } elseif ([DateTime]::UtcNow -ge $deadline) {
            throw "No $($names.process) within $LaunchTimeoutSeconds seconds; keep the session locked and inspect it"
        }
        Write-LaunchCandidateLog
        if (-not $p) { Start-Sleep 1 }
    }
} finally { Write-LaunchCandidateLog }
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
if ($modLogs.Count) {
    # The mod appends its default blocks to <GAME>MEVR.ini on its first tick, a few seconds after the XR marker; a load
    # whose hashes were frozen before that was refused. Wait for the settings markers and a quiet file. A timeout only
    # warns: load.ps1 still refuses any change during the load. Gotcha mod-settings-appended-after-xr-ready.
    $modSettings = Join-Path $bin "ASI\${Game}MEVR.ini"
    $settled = Wait-RunModSettings -Path $modSettings -Logs $modLogs -Before $oldLogs -Started $started -Guard {
        Assert-RunLock $Holder
        $live = Get-Process -Id $p.pid -ErrorAction SilentlyContinue
        if (-not $live) { throw 'The game exited while its settings were written' }
        Assert-RunProcess $live $open.gameProcess $exe
    }
    $line = Format-RunModSettings $settled $modSettings
    Add-Content -LiteralPath (Join-Path $open.session 'mod-settings.txt') -Value "[$((Get-Date).ToString('HH:mm:ss.fff'))] $line" -Encoding utf8
    if ($settled.settled) {
        $line
        try { [IO.File]::WriteAllText((Join-Path $open.session 'effective-mod.ini'), (Read-RunLog $modSettings)) }
        catch { Write-Warning "effective settings not copied: $($_.Exception.Message)" }
    } else { Write-Warning $line }
}
if ($RendererPreview) {
    $previewReceipt.freshMarker = $true; $previewReceipt.log = $log
    # The renderer can migrate an obsolete INI mode while starting. Never label
    # that run with the requested mode or continue on changed runtime artifacts.
    $initialized = Wait-PreviewInitializedConfig -Bin $bin -Artifacts $open.previewArtifacts -StartupLogs $settingsLogs `
        -Before $oldLogs -Started $started -Mode $previewOptions.mode -Diagnostics $previewOptions.diagnostics `
        -MirrorEvery $previewOptions.mirrorEvery -EyeFrustum $previewOptions.eyeFrustum -RenderSize $previewOptions.renderSize -Guard {
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
