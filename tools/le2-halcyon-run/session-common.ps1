# SPDX-License-Identifier: MIT
# Shared validation for this existing runner. No action is performed when dot-sourced.
function Read-RunLog([string]$Path) {
    # The live host keeps a writer open (_SH_DENYWR). Readers must share that writer;
    # File.ReadAllText's default sharing conflicts even though the host permits reads.
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object IO.StreamReader($stream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function Get-RunNames([string]$Game) {
    if ($Game -notin @('LE2', 'LE3')) { throw 'Game must be LE2 or LE3' }
    $n = $Game.Substring(2)
    [pscustomobject]@{ game = $Game; number = $n; folder = "ME$n"; process = "MassEffect$n";
        current = (Join-Path $env:LOCALAPPDATA "MEVR\sessions\$($Game.ToLower())-current.json") }
}
function Assert-RunMode([string]$Game, [bool]$DesktopDump, [bool]$EngineProbe = $false, [bool]$EngineRequests = $false, [bool]$RendererPreview = $false) {
    if ($Game -notin @('LE2', 'LE3')) { throw 'Unsupported game mode' }
    if ($RendererPreview -and ($Game -ne 'LE3' -or $DesktopDump -or $EngineProbe -or $EngineRequests)) {
        throw 'RendererPreview is LE3 only and cannot be combined with desktop/probe/request modes'
    }
    if ($Game -eq 'LE3' -and -not ($DesktopDump -or $RendererPreview)) { throw 'LE3 requires explicit DesktopDump or RendererPreview mode' }
    if ($Game -eq 'LE2' -and ($DesktopDump -or $RendererPreview)) { throw 'LE2 supports the existing VR mode only' }
    if ($EngineRequests -and -not $EngineProbe) { throw 'EngineRequests requires EngineProbe' }
    if ($EngineProbe -and ($Game -ne 'LE3' -or -not $DesktopDump)) { throw 'EngineProbe requires LE3 DesktopDump protection' }
}
function Assert-RunSession($Open, [string]$Holder, [string]$Game, [bool]$DesktopDump, [bool]$EngineProbe = $false, [bool]$EngineRequests = $false, [bool]$RendererPreview = $false) {
    Assert-RunMode $Game $DesktopDump $EngineProbe $EngineRequests $RendererPreview
    if (-not $Holder -or $Open.holder -ne $Holder) { throw 'Session holder does not match -Holder' }
    $recordedGame = if ($Open.PSObject.Properties['game']) { $Open.game } else { 'LE2' }
    $recordedDump = if ($Open.PSObject.Properties['desktopDump']) { $Open.desktopDump } else { $false }
    if ($recordedDump -isnot [bool]) { throw 'desktopDump metadata must be a JSON boolean' }
    if ($recordedGame -ne $Game -or $recordedDump -ne $DesktopDump) { throw 'Session game/mode differs from the requested game/mode' }
    $recordedProbe = if ($Open.PSObject.Properties['engineProbe']) { $Open.engineProbe } else { $false }
    if ($recordedProbe -isnot [bool] -or $recordedProbe -ne $EngineProbe) { throw 'Session engine-probe mode differs from request' }
    $recordedRequests = if ($Open.PSObject.Properties['engineRequests']) { $Open.engineRequests } else { $false }
    if ($recordedRequests -isnot [bool] -or $recordedRequests -ne $EngineRequests) { throw 'Session request mode differs from request' }
    $recordedPreview = if ($Open.PSObject.Properties['rendererPreview']) { $Open.rendererPreview } else { $false }
    if ($recordedPreview -isnot [bool] -or $recordedPreview -ne $RendererPreview) { throw 'Session renderer-preview mode differs from request' }
    if ($RendererPreview -and (-not $Open.PSObject.Properties['desktopDump'] -or -not $Open.PSObject.Properties['engineProbe'] -or
        -not $Open.PSObject.Properties['engineRequests'])) { throw 'Preview requires explicit false desktop/probe/request metadata' }
}
function Assert-RunLock([string]$Holder) {
    $path = Join-Path $env:LOCALAPPDATA 'MEVR\game-run.lock'
    if (-not (Test-Path -LiteralPath $path)) { throw 'No owned game-run lock' }
    $held = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($held.holder -ne $Holder) { throw 'Game-run lock belongs to another holder' }
}
function Resolve-RunChild([string]$Root, [string]$Relative) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    # Manifest relatives historically begin with a slash; fully qualified session paths also occur.
    $path = if ([IO.Path]::IsPathRooted($Relative) -and $Relative -match '^[A-Za-z]:|^\\\\') {
        [IO.Path]::GetFullPath($Relative)
    } else { [IO.Path]::GetFullPath((Join-Path $base $Relative.TrimStart('\', '/'))) }
    if (-not $path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Run path escapes its intended root' }
    return $path
}
function Read-RunSession([string]$Holder, [string]$Game, [bool]$DesktopDump, [bool]$EngineProbe = $false, [bool]$EngineRequests = $false, [bool]$RendererPreview = $false) {
    $names = Get-RunNames $Game
    if (-not (Test-Path -LiteralPath $names.current)) { throw "No $Game stretch is open" }
    $open = Get-Content -LiteralPath $names.current -Raw | ConvertFrom-Json
    Assert-RunSession $open $Holder $Game $DesktopDump $EngineProbe $EngineRequests $RendererPreview
    Assert-RunLock $Holder
    $sessions = Join-Path $env:LOCALAPPDATA 'MEVR\sessions'
    $safe = Resolve-RunChild $sessions $open.session
    if ($safe -ne [IO.Path]::GetFullPath($open.session) -or -not (Test-Path -LiteralPath $safe)) { throw 'Invalid session directory' }
    Assert-RunUnlinkedPath $safe
    return $open
}
function Write-RunSession($Open, [string]$Current) {
    $temporary = "$Current.tmp"
    $Open | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Current -Force
}
function Backup-RunConsole($Open, [string]$Current, [string]$ChannelRoot,
    [string[]]$Names = @('console.request', 'console.result'), [string]$Metadata = 'consoleBefore', [string]$Directory = 'console-before') {
    Assert-RunUnlinkedPath $ChannelRoot
    foreach ($name in $Names) { Assert-RunUnlinkedPath (Join-Path $ChannelRoot $name); if ($name -notmatch '^[A-Za-z0-9_-]+\.(request|result|json)$') { throw 'Invalid channel name' } }
    $backup = Resolve-RunChild $Open.session $Directory
    if (-not $Open.PSObject.Properties[$Metadata]) {
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        $before = @{}
        foreach ($name in $Names) {
            $path = Join-Path $ChannelRoot $name
            $before[$name] = $null
            if (Test-Path -LiteralPath $path) {
                $hash = (Get-FileHash -LiteralPath $path).Hash
                Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name)
                if ((Get-FileHash -LiteralPath (Join-Path $backup $name)).Hash -ne $hash) { throw 'Console backup mismatch' }
                $before[$name] = $hash
            }
        }
        $Open | Add-Member -NotePropertyName $Metadata -NotePropertyValue ([pscustomobject]$before)
        Write-RunSession $Open $Current
    }
    foreach ($name in $Names) {
        $property = $Open.$Metadata.PSObject.Properties[$name]
        if ($null -eq $property) { throw 'Incomplete channel metadata' }
        if ($null -ne $property.Value -and (Get-FileHash -LiteralPath (Join-Path $backup $name)).Hash -ne $property.Value) { throw 'Channel backup changed' }
    }
    # Clear stale requests before the diagnostic consumer starts. Preserve every generation.
    foreach ($name in $Names) {
        $path = Join-Path $ChannelRoot $name
        if (Test-Path -LiteralPath $path) {
            Move-Item -LiteralPath $path -Destination (Join-Path $backup ([Guid]::NewGuid().ToString('N') + '-' + $name))
        }
    }
}
function Restore-RunConsole($Open, [string]$ChannelRoot,
    [string[]]$Names = @('console.request', 'console.result'), [string]$Metadata = 'consoleBefore', [string]$Directory = 'console-before') {
    if (-not $Open.PSObject.Properties[$Metadata]) { throw 'Missing console channel backup metadata' }
    Assert-RunUnlinkedPath $ChannelRoot
    foreach ($name in $Names) { Assert-RunUnlinkedPath (Join-Path $ChannelRoot $name); if ($name -notmatch '^[A-Za-z0-9_-]+\.(request|result|json)$') { throw 'Invalid channel name' } }
    $backup = Resolve-RunChild $Open.session $Directory
    foreach ($name in $Names) {
        $property = $Open.$Metadata.PSObject.Properties[$name]
        if ($null -eq $property) { throw 'Incomplete console channel backup metadata' }
        $hash = $property.Value
        if ($null -ne $hash -and ($hash -notmatch '^[0-9A-Fa-f]{64}$' -or
            (Get-FileHash -LiteralPath (Join-Path $backup $name)).Hash -ne $hash)) { throw 'Console backup changed' }
    }
    foreach ($name in $Names) {
        $path = Join-Path $ChannelRoot $name
        if (Test-Path -LiteralPath $path) {
            Move-Item -LiteralPath $path -Destination (Join-Path $backup ([Guid]::NewGuid().ToString('N') + '-after-' + $name))
        }
        $hash = $Open.$Metadata.PSObject.Properties[$name].Value
        if ($null -ne $hash) {
            Copy-Item -LiteralPath (Join-Path $backup $name) -Destination $path
            if ((Get-FileHash -LiteralPath $path).Hash -ne $hash) { throw 'Console restoration mismatch' }
        }
    }
}
function Get-RunManifest([string]$Root) {
    @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | ForEach-Object {
        [pscustomobject]@{ rel = $_.FullName.Substring($Root.TrimEnd('\').Length); hash = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
}
function Assert-RunTree([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { throw 'Expected a directory for backup/restore' }
    if (((Get-Item -LiteralPath $Root).Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        @(Get-ChildItem -LiteralPath $Root -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw 'Backup/restore tree contains a reparse point'
    }
}
function Backup-RunRoot([string]$Live, [string]$Backup, [string]$Manifest) {
    # Reparse traversal could escape the selected profile/install; refuse before copying.
    Assert-RunTree $Live
    $before = @(Get-RunManifest $Live)
    & robocopy $Live $Backup /E /COPY:DAT /DCOPY:T /R:2 /W:1 /NFL /NDL /NJH /NJS /NP /XJ | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "Backup robocopy failed: exit $LASTEXITCODE" }
    $after = @(Get-RunManifest $Live)
    $copied = @(Get-RunManifest $Backup)
    if ($before.Count -ne $after.Count -or $before.Count -ne $copied.Count) { throw 'Backup file count mismatch' }
    foreach ($entry in $before) {
        $source = Resolve-RunChild $Live $entry.rel
        $dest = Resolve-RunChild $Backup $entry.rel
        if ((Get-FileHash -LiteralPath $source).Hash -ne $entry.hash -or (Get-FileHash -LiteralPath $dest).Hash -ne $entry.hash) {
            throw 'Backup checksum mismatch or source changed while backing up'
        }
    }
    ConvertTo-Json -InputObject $before -Depth 3 | Set-Content -LiteralPath $Manifest -Encoding utf8
}
function Assert-RunBackup([string]$Live, [string]$Backup, [string]$Manifest) {
    Assert-RunTree $Live
    Assert-RunTree $Backup
    $raw = Get-Content -LiteralPath $Manifest -Raw
    $parsed = $raw | ConvertFrom-Json
    $entries = @($parsed)
    if (-not $entries.Count -and $raw -notmatch '^\s*\[\s*\]\s*$') { throw 'Missing backup manifest entries' }
    $seen = @{}
    foreach ($e in $entries) {
        if (-not $e.rel -or $e.hash -notmatch '^[0-9a-fA-F]{64}$' -or $seen.ContainsKey($e.rel)) { throw 'Invalid backup manifest entry' }
        $seen[$e.rel] = $true
        $null = Resolve-RunChild $Live $e.rel
        $source = Resolve-RunChild $Backup $e.rel
        if ((Get-FileHash -LiteralPath $source).Hash -ne $e.hash) { throw 'Backup changed; restore refused' }
    }
    if (@(Get-RunManifest $Backup).Count -ne $entries.Count) { throw 'Incomplete backup manifest; restore refused' }
}
function Get-RunProcessIdentity($Process) {
    [pscustomobject]@{ pid = $Process.Id; path = $Process.Path; startedUtc = $Process.StartTime.ToUniversalTime().ToString('o') }
}
function Get-RunLaunchIdentity([int]$ProcessId, [string]$ExpectedPath, [DateTime]$RequestedUtc) {
    # Name enumeration may outlive a short-lived launcher. Resolve its ID again before reading identity.
    $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if (-not $process) { return $null }
    $path = $null; $started = $null
    try { $path = $process.Path; $started = $process.StartTime.ToUniversalTime() } catch { }
    # Null Path / unavailable StartTime is common after exit, but is not permission to adopt a live process.
    $still = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if (-not $still -or $still.HasExited) { return $null }
    $stillPath = $null; $stillStarted = $null
    try { $stillPath = $still.Path; $stillStarted = $still.StartTime.ToUniversalTime() } catch { }
    if ($still.HasExited) { return $null }
    $observed = "pid=$ProcessId path='$stillPath' startedUtc='$(if ($stillStarted) { $stillStarted.ToString('o') } else { 'unavailable' })'"
    if (-not $path -or -not $started -or -not $stillPath -or -not $stillStarted -or
        $path -ne $stillPath -or $started -ne $stillStarted) {
        throw "Live process identity unavailable or changed; not adopted ($observed)"
    }
    if ($stillPath -ne $ExpectedPath -or $stillStarted -lt $RequestedUtc) {
        throw "Unexpected live process; not adopted ($observed; expectedPath='$ExpectedPath' requestedUtc='$($RequestedUtc.ToString('o'))')"
    }
    [pscustomobject]@{ pid = $ProcessId; path = $stillPath; startedUtc = $stillStarted.ToString('o') }
}
function Assert-RunProcess($Process, $Recorded, [string]$ExpectedPath) {
    if (-not $Recorded -or $Process.Id -ne $Recorded.pid -or
        $Process.Path -ne $ExpectedPath -or $Process.Path -ne $Recorded.path -or
        $Process.StartTime.ToUniversalTime().ToString('o') -ne $Recorded.startedUtc) { throw 'Unowned or changed process: refusing to stop it' }
}
function Assert-RunDumpHost([string]$HostPath, [bool]$EngineProbe = $false) {
    $marker = if ($EngineProbe) { 'engine-probe: host renderer disabled' } else { 'dump-only: code dump attempted; renderer and hooks disabled' }
    if (-not [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($HostPath)).Contains($marker)) {
        throw 'Selected host lacks the requested development desktop gate; refusing a possible VR launch'
    }
}
function Assert-RunDesktopConfig([string]$Config, [bool]$EngineProbe, [bool]$EngineRequests = $false) {
    $key = if ($EngineProbe) { 'engineProbe' } else { 'dumpOnly' }
    $suffix = if ($EngineRequests -and $EngineProbe) { '\s+engineProbeRequests\s*=\s*1' } else { '' }
    if (($EngineRequests -and -not $EngineProbe) -or $Config.Trim() -notmatch ('^\[Debug\]\s+' + $key + '\s*=\s*1' + $suffix + '$')) {
        throw 'Desktop diagnostic configuration changed since prep'
    }
}
function Confirm-RunReflection([string]$Path, [DateTime]$Started) {
    if ((Get-Item -LiteralPath $Path).LastWriteTimeUtc -lt $Started) { throw 'Stale reflection report' }
    $report = Read-RunLog $Path | ConvertFrom-Json
    foreach ($key in @('checks', 'agree', 'mismatches', 'notReflected')) {
        if ($null -eq $report.$key -or $report.$key -is [string] -or $report.$key -is [bool] -or
            $report.$key -lt 0 -or [math]::Floor($report.$key) -ne $report.$key) { throw 'Invalid reflection counts' }
    }
    $rows = @($report.rows)
    if ($report.checks -lt 1 -or $report.agree -lt 1 -or $rows.Count -ne $report.checks -or
        $report.agree + $report.mismatches + $report.notReflected -ne $report.checks) { throw 'Incomplete reflection report' }
    foreach ($row in $rows) {
        if ($row.agree -isnot [bool] -or $row.notReflected -isnot [bool]) { throw 'Invalid reflection row' }
    }
    if (@($rows | Where-Object { $_.agree }).Count -ne $report.agree -or
        @($rows | Where-Object { -not $_.agree -and $_.notReflected }).Count -ne $report.notReflected -or
        @($rows | Where-Object { -not $_.agree -and -not $_.notReflected }).Count -ne $report.mismatches) {
        throw 'Reflection rows disagree with totals'
    }
    if ($report.mismatches -ne 0) { throw "Reflection probe found $($report.mismatches) mismatches; inspect the report" }
    [pscustomobject]@{ path = $Path; sha256 = (Get-FileHash -LiteralPath $Path).Hash;
        checks = $report.checks; agree = $report.agree; notReflected = $report.notReflected;
        note = 'Reflection cross-check only; not gameplay or VR acceptance' }
}
function Get-RunTextIdentity([string]$Exe) {
    # Only the bounded PE header fields that CodeDump writes; no code disassembly.
    $bytes = [IO.File]::ReadAllBytes($Exe)
    if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) { throw 'Invalid executable DOS header' }
    $nt = [BitConverter]::ToInt32($bytes, 60)
    if ($nt -lt 64 -or $nt -gt $bytes.Length - 24 -or [BitConverter]::ToUInt32($bytes, $nt) -ne 0x4550) { throw 'Invalid PE header' }
    $sections = [BitConverter]::ToUInt16($bytes, $nt + 6)
    $table = $nt + 24 + [BitConverter]::ToUInt16($bytes, $nt + 20)
    if ($sections -lt 1 -or $sections -gt 96 -or $table + 40 * $sections -gt $bytes.Length) { throw 'Invalid PE section table' }
    $matches = @()
    for ($i = 0; $i -lt $sections; $i++) {
        $at = $table + 40 * $i
        if ([Text.Encoding]::ASCII.GetString($bytes, $at, 8).TrimEnd([char]0) -eq '.text') {
            $matches += [pscustomobject]@{ timestamp = [BitConverter]::ToUInt32($bytes, $nt + 8);
                text_rva = [BitConverter]::ToUInt32($bytes, $at + 12); text_size = [BitConverter]::ToUInt32($bytes, $at + 8) }
        }
    }
    if ($matches.Count -ne 1 -or $matches[0].text_size -eq 0) { throw 'Expected one nonempty PE .text section' }
    return $matches[0]
}
function Confirm-RunDump([string]$Exe, [string]$DumpRoot) {
    $identity = Get-RunTextIdentity $Exe
    $stem = 'MassEffect-{0:X8}' -f $identity.timestamp
    $binary = Join-Path $DumpRoot "$stem.text.bin"
    $meta = Join-Path $DumpRoot "$stem.txt"
    $fields = @{}
    foreach ($line in Get-Content -LiteralPath $meta) {
        if ($line -match '^([^=]+)=(.*)$') {
            if ($fields.ContainsKey($Matches[1])) { throw 'Duplicate dump metadata key' }
            $fields[$Matches[1]] = $Matches[2]
        }
    }
    foreach ($key in @('timestamp', 'text_rva', 'text_size')) {
        if ($fields[$key] -notmatch '^0x[0-9a-fA-F]+$' -or [Convert]::ToUInt32($fields[$key].Substring(2), 16) -ne $identity.$key) {
            throw "Dump metadata differs from executable: $key"
        }
    }
    if ($fields['patched_starts'] -notmatch '^\d+$' -or (Get-Item -LiteralPath $binary).Length -ne $identity.text_size) { throw 'Invalid dump size or patch metadata' }
    $patchCount = [int]$fields['patched_starts']
    if (-not $fields.ContainsKey('patched')) { throw 'Missing patch list' }
    $patches = if ($fields['patched']) { @($fields['patched'].Split(',')) } else { @() }
    if ($patches.Count -ne $patchCount) { throw 'Dump patch count/list disagree' }
    foreach ($patch in $patches) {
        if ($patch -notmatch '^0x[0-9a-fA-F]+$') { throw 'Invalid patch RVA' }
        $rva = [Convert]::ToUInt32($patch.Substring(2), 16)
        if ($rva -lt $identity.text_rva -or [long]$rva -ge [long]$identity.text_rva + $identity.text_size) { throw 'Patch outside .text' }
    }
    [pscustomobject]@{ exeSha256 = (Get-FileHash -LiteralPath $Exe).Hash; binary = $binary; metadata = $meta;
        binarySha256 = (Get-FileHash -LiteralPath $binary).Hash; metadataSha256 = (Get-FileHash -LiteralPath $meta).Hash;
        writtenUtc = (Get-Item -LiteralPath $binary).LastWriteTimeUtc.ToString('o'); patchedStarts = $patchCount;
        note = 'Header/size/hash match only; dump may predate this launch and is not reflection or runtime acceptance' }
}
# Preview-only additions below. Existing desktop paths never invoke VR/environment helpers.
function Get-PreviewChannels {
    @('snapshot.request', 'record.request', 'replay.request', 'perf.request', 'eyediff.request',
      'mainmenu.request', 'voice.request', 'setup.request')
}
function Get-PreviewPaths {
    $steam = 'C:\Program Files (x86)\Steam'
    $vr = Join-Path $steam 'steamapps\common\SteamVR'
    [ordered]@{
        null = (Join-Path $vr 'drivers\null\resources\settings\default.vrsettings')
        steam = (Join-Path $steam 'config\steamvr.vrsettings')
        chaperone = (Join-Path $steam 'config\chaperone_info.vrchap')
        le1 = (Join-Path $steam 'steamapps\common\Mass Effect Legendary Edition\Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini')
    }
}
function Assert-PreviewIdle($Paths, [string]$GlobalUndo) {
    if (Test-Path -LiteralPath $GlobalUndo) { throw 'Another fake-headset undo record exists; preview cannot take ownership' }
    if (Get-Process vrserver, vrmonitor, vrcompositor, vrdashboard, vrwebhelper, vrstartup -ErrorAction SilentlyContinue) {
        throw 'SteamVR is already running; preview cannot replace another VR session'
    }
    $steam = Get-Content -LiteralPath $Paths.steam -Raw | ConvertFrom-Json
    $nullDriver = Get-Content -LiteralPath $Paths.null -Raw | ConvertFrom-Json
    if ($nullDriver.driver_null.enable -isnot [bool]) { throw 'Cannot establish the null driver is disabled' }
    if (($steam.PSObject.Properties['steamvr'] -and $steam.steamvr.PSObject.Properties['forcedDriver'] -and $steam.steamvr.forcedDriver -eq 'null') -or
        $nullDriver.driver_null.enable -eq $true) { throw 'Null driver is already enabled; not this session to undo' }
}
function Assert-RunUnlinkedPath([string]$Path) {
    $probe = [IO.Path]::GetFullPath($Path)
    while ($probe) {
        if ((Test-Path -LiteralPath $probe) -and ((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Linked path refused for protected file backup/restore'
        }
        $parent = [IO.Path]::GetDirectoryName($probe)
        if ($parent -eq $probe) { break }
        $probe = $parent
    }
}
function Backup-PreviewEnvironment([string]$Session, $Paths) {
    Assert-RunUnlinkedPath $Session
    foreach ($path in $Paths.Values) { Assert-RunUnlinkedPath $path }
    $backup = Resolve-RunChild $Session 'preview-environment'
    New-Item -ItemType Directory -Path $backup | Out-Null
    $rows = @()
    foreach ($key in $Paths.Keys) {
        $path = [IO.Path]::GetFullPath($Paths[$key]); $hash = $null
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe environment file' }
            $hash = (Get-FileHash -LiteralPath $path).Hash
            Copy-Item -LiteralPath $path -Destination (Join-Path $backup $key)
            if ((Get-FileHash -LiteralPath (Join-Path $backup $key)).Hash -ne $hash -or (Get-FileHash -LiteralPath $path).Hash -ne $hash) {
                throw 'Environment backup mismatch'
            }
        }
        $rows += [pscustomobject]@{ key = $key; path = $path; hash = $hash }
    }
    return $rows
}
function Assert-PreviewEnvironment($Open, $Paths) {
    if (-not $Open.PSObject.Properties['previewEnvironment'] -or @($Open.previewEnvironment).Count -ne $Paths.Count) { throw 'Incomplete preview environment backup' }
    $backup = Resolve-RunChild $Open.session 'preview-environment'
    Assert-RunUnlinkedPath $backup
    Assert-RunTree $backup
    foreach ($path in $Paths.Values) { Assert-RunUnlinkedPath $path }
    $seen = @{}
    foreach ($row in $Open.previewEnvironment) {
        if (-not $Paths.Contains($row.key) -or $seen.ContainsKey($row.key) -or
            $row.path -ine [IO.Path]::GetFullPath($Paths[$row.key])) { throw 'Preview environment path differs from fixed source' }
        $seen[$row.key] = $true
        if ($null -ne $row.hash -and ($row.hash -notmatch '^[A-Fa-f0-9]{64}$' -or
            (Get-FileHash -LiteralPath (Resolve-RunChild $backup $row.key)).Hash -ne $row.hash)) { throw 'Preview environment backup changed' }
    }
}
function Restore-PreviewEnvironment($Open, $Paths) {
    Assert-PreviewEnvironment $Open $Paths
    $backup = Resolve-RunChild $Open.session 'preview-environment'
    $after = Resolve-RunChild $Open.session ('preview-after-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $after | Out-Null
    foreach ($row in $Open.previewEnvironment) {
        if (Test-Path -LiteralPath $row.path) { Move-Item -LiteralPath $row.path -Destination (Join-Path $after $row.key) }
        if ($null -ne $row.hash) { Copy-Item -LiteralPath (Resolve-RunChild $backup $row.key) -Destination $row.path }
    }
    foreach ($row in $Open.previewEnvironment) {
        if ($null -eq $row.hash) {
            if (Test-Path -LiteralPath $row.path) { throw 'Preview environment absence was not restored' }
        } elseif ((Get-FileHash -LiteralPath $row.path).Hash -ne $row.hash) { throw 'Preview environment restoration hash mismatch' }
    }
    [pscustomobject]@{ exactFilesRestored = $true; differences = 0; files = $Open.previewEnvironment;
        restoredUtc = [DateTime]::UtcNow.ToString('o') } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $Open.session 'preview-environment-restored.json') -Encoding utf8
}
function Get-PreviewVrProcesses($Open, [bool]$AllowNewHelpers = $false) {
    $processes = @(Get-Process vrserver, vrmonitor, vrcompositor, vrdashboard, vrwebhelper, vrstartup -ErrorAction SilentlyContinue)
    if (@($processes | Where-Object { $_.ProcessName -eq 'vrserver' }).Count -gt 1) { throw 'Multiple VR servers appeared' }
    $recorded = @(if ($Open.PSObject.Properties['previewProcesses']) { $Open.previewProcesses })
    if (-not $Open.previewStartedUtc -and $processes.Count) { throw 'VR processes appeared before the owned preview start' }
    $vrRoot = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\'
    $seen = @()
    foreach ($p in $processes) {
        $path = $p.Path
        if (-not $path -or -not $path.StartsWith($vrRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected VR process executable path' }
        $old = @($recorded | Where-Object { $_.pid -eq $p.Id })
        if ($old.Count -eq 1) { Assert-RunProcess $p $old[0] $path }
        elseif (-not $AllowNewHelpers -or ($p.ProcessName -eq 'vrserver' -and @($recorded | Where-Object { $_.name -eq 'vrserver' }).Count)) {
            throw 'Unrecorded/replaced VR process; refusing to adopt or stop it'
        }
        $identity = Get-RunLaunchIdentity $p.Id $path ([DateTime]::Parse($Open.previewStartedUtc).ToUniversalTime())
        if ($identity) { $identity | Add-Member -NotePropertyName name -NotePropertyValue $p.ProcessName; $seen += $identity }
    }
    # Newly appearing helpers are only admissible while the original vrserver still owns this runtime.
    $recordedIds = @($recorded | ForEach-Object { $_.pid })
    if ($recorded.Count -and $AllowNewHelpers -and @($seen | Where-Object { $_.name -eq 'vrserver' }).Count -ne 1 -and
        @($seen | Where-Object { $recordedIds -notcontains $_.pid }).Count) {
        throw 'New VR helpers without the original owned server'
    }
    return $seen
}
function Get-PreviewRendererConfig {
    # me2_menu.cpp LoadValues + its lowest displayed render target (3072x1728);
    # d3d_capture.cpp reads RenderW/H beside MassEffect3.exe before rendering.
    "[VR]`r`nEnabled=1`r`nMode=4`r`nRenderW=3072`r`nRenderH=1728`r`nHeadAim=0`r`nHeadLook=1`r`nMoveFollowsHead=1`r`nDecoupledPitch=1`r`nPosEnabled=1`r`nPosScale=2.0`r`nDiagnostics=0`r`nCineVrConvo=0`r`nCineVrCutscene=0`r`n[FirstPerson]`r`nConvoFirstPerson=0`r`n"
}
function Get-PreviewModConfig {
    "[Debug]`r`nrequests = 1`r`nopenxrRuntime = C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json`r`n[Pointer]`r`nsetCursor = 0`r`n"
}
function Assert-PreviewConfig([string]$Bin) {
    if ([IO.File]::ReadAllText((Join-Path $Bin 'ASI\LE3MEVR.ini')) -cne (Get-PreviewModConfig) -or
        [IO.File]::ReadAllText((Join-Path $Bin 'MELE3VR.ini')) -cne (Get-PreviewRendererConfig)) { throw 'Preview configuration changed since prep' }
}
function Get-PreviewArtifacts([string]$Bin) {
    $paths = @((Join-Path $Bin 'MassEffect3.exe'), (Join-Path $Bin 'ASI\LE3MEVR.asi'),
        (Join-Path $Bin 'ASI\LE3MEVR.ini'), (Join-Path $Bin 'MELE3VR.ini'))
    $modulePath = Join-Path $Bin 'ASI\LE3MEVR.modpath'
    if (Test-Path -LiteralPath $modulePath) {
        $module = [IO.File]::ReadAllText($modulePath).Trim()
        if (-not [IO.Path]::IsPathRooted($module) -or -not (Test-Path -LiteralPath $module -PathType Leaf)) { throw 'Preview modpath does not identify a real module' }
        $paths += $modulePath; $paths += $module
    }
    @($paths | ForEach-Object { [pscustomobject]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_).Hash } })
}
function Assert-RunArtifacts($Artifacts) {
    if (-not @($Artifacts).Count) { throw 'Missing runtime artifact identities' }
    foreach ($artifact in $Artifacts) {
        if ((Get-FileHash -LiteralPath $artifact.path).Hash -ne $artifact.sha256) { throw 'Runtime artifact changed since prep' }
    }
}
function Get-RunRendererLog([string]$Game) {
    if ($Game -eq 'LE3') { return Join-Path $env:LOCALAPPDATA 'MELEVR\ME3PassiveDxgiProbe.log' }
    return Join-Path $env:LOCALAPPDATA 'MELE2VR\MELE2VR_Log.txt'
}
function Test-RunFreshMarker([string]$Path, [string]$Before, [DateTime]$Started, [string]$Marker) {
    if (-not (Test-Path -LiteralPath $Path) -or (Get-Item -LiteralPath $Path).LastWriteTimeUtc -lt $Started) { return $false }
    $text = Read-RunLog $Path
    $fresh = if ($Before -and $text.StartsWith($Before, [StringComparison]::Ordinal)) { $text.Substring($Before.Length) } else { $text }
    return $fresh.Contains($Marker)
}
