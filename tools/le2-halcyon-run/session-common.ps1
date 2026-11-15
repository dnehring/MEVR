# SPDX-License-Identifier: MIT
# Shared validation for this existing runner. No action is performed when dot-sourced.
function Get-RunNames([string]$Game) {
    if ($Game -notin @('LE2', 'LE3')) { throw 'Game must be LE2 or LE3' }
    $n = $Game.Substring(2)
    [pscustomobject]@{ game = $Game; number = $n; folder = "ME$n"; process = "MassEffect$n";
        current = (Join-Path $env:LOCALAPPDATA "MEVR\sessions\$($Game.ToLower())-current.json") }
}
function Assert-RunMode([string]$Game, [bool]$DesktopDump) {
    if (($Game -eq 'LE3') -ne $DesktopDump) { throw 'LE3 requires -DesktopDump; LE2 supports the existing VR mode only' }
}
function Assert-RunSession($Open, [string]$Holder, [string]$Game, [bool]$DesktopDump) {
    Assert-RunMode $Game $DesktopDump
    if (-not $Holder -or $Open.holder -ne $Holder) { throw 'Session holder does not match -Holder' }
    $recordedGame = if ($Open.PSObject.Properties['game']) { $Open.game } else { 'LE2' }
    $recordedDump = if ($Open.PSObject.Properties['desktopDump']) { $Open.desktopDump } else { $false }
    if ($recordedDump -isnot [bool]) { throw 'desktopDump metadata must be a JSON boolean' }
    if ($recordedGame -ne $Game -or $recordedDump -ne $DesktopDump) { throw 'Session game/mode differs from the requested game/mode' }
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
function Read-RunSession([string]$Holder, [string]$Game, [bool]$DesktopDump) {
    $names = Get-RunNames $Game
    if (-not (Test-Path -LiteralPath $names.current)) { throw "No $Game stretch is open" }
    $open = Get-Content -LiteralPath $names.current -Raw | ConvertFrom-Json
    Assert-RunSession $open $Holder $Game $DesktopDump
    Assert-RunLock $Holder
    $sessions = Join-Path $env:LOCALAPPDATA 'MEVR\sessions'
    $safe = Resolve-RunChild $sessions $open.session
    if ($safe -ne [IO.Path]::GetFullPath($open.session) -or -not (Test-Path -LiteralPath $safe)) { throw 'Invalid session directory' }
    return $open
}
function Write-RunSession($Open, [string]$Current) {
    $temporary = "$Current.tmp"
    $Open | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $Current -Force
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
function Assert-RunProcess($Process, $Recorded, [string]$ExpectedPath) {
    if (-not $Recorded -or $Process.Id -ne $Recorded.pid -or
        $Process.Path -ne $ExpectedPath -or $Process.Path -ne $Recorded.path -or
        $Process.StartTime.ToUniversalTime().ToString('o') -ne $Recorded.startedUtc) { throw 'Unowned or changed process: refusing to stop it' }
}
function Assert-RunDumpHost([string]$HostPath) {
    $marker = 'dump-only: code dump attempted; renderer and hooks disabled'
    if (-not [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($HostPath)).Contains($marker)) {
        throw 'Selected host lacks the development dump-only gate; refusing a possible VR launch'
    }
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
