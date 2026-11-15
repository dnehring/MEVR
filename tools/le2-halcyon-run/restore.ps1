# SPDX-License-Identifier: MIT
# Ends the selected game's stretch: logs kept, LE2's fake headset off, the three backed-up roots put back to
# their manifests (changed files copied back, files the runs added moved aside into the session folder, never deleted),
# every file checked hash for hash, the run lock released. Once per task, not after every run.
param([Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2', [switch]$DesktopDump, [switch]$EngineProbe)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\session-common.ps1"
Assert-RunMode $Game ([bool]$DesktopDump) ([bool]$EngineProbe)
$open = Read-RunSession $Holder $Game ([bool]$DesktopDump) ([bool]$EngineProbe)
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$names = Get-RunNames $Game
$current = $names.current
$S = $open.session
$install = (Find-MELEInstall).Root
$G = Join-Path $install 'Game'
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'
if (($open.install -and $open.install -ne $install) -or ($open.profile -and $open.profile -ne $docs)) { throw 'Install/profile changed since backup' }
if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) { throw 'A game is running: close only your owned session before restoring' }

New-Item -ItemType Directory -Force "$S\logs" | Out-Null
Copy-Item "$G\$($names.folder)\Binaries\Win64\Logs\${Game}MEVR.log" "$S\logs\" -ErrorAction SilentlyContinue
if (-not $DesktopDump) { Copy-Item "$env:LOCALAPPDATA\MELE2VR\MELE2VR_Log.txt" "$S\logs\" -ErrorAction SilentlyContinue }
Copy-Item "$env:LOCALAPPDATA\MEVR\crash-breadcrumbs.txt" "$S\logs\" -ErrorAction SilentlyContinue
if ($EngineProbe -and $open.launchRequestedUtc) {
    $reportPath = Join-Path $env:LOCALAPPDATA 'MEVR\reflect-check.json'
    if ((Test-Path -LiteralPath $reportPath) -and
        (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -ge [DateTime]::Parse($open.launchRequestedUtc).ToUniversalTime()) {
        # Preserve failing reports too. This copy is evidence, never an acceptance flag.
        Copy-Item -LiteralPath $reportPath -Destination (Join-Path $S 'reflect-check.json')
    }
}
"logs kept in $S\logs"

if (-not $DesktopDump) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\fake-headset.ps1" -Off
    if ($LASTEXITCODE -ne 0) { throw 'Fake-headset restore failed; session stays open' }
}

$roots = @(@($docs, 'profile'), @("$G\$($names.folder)\BioGame\Config", "$($names.folder)Config"), @("$G\$($names.folder)\Binaries\Win64", "$($names.folder)Win64"))
# Validate every manifest and backup before modifying any live root.
foreach ($r in $roots) {
    Assert-RunBackup $r[0] "$S\backup\$($r[1])" "$S\before-$($r[1]).json"
}
foreach ($r in $roots) {
    $live = $r[0]; $tag = $r[1]
    $before = @{}; foreach ($e in (Get-Content "$S\before-$tag.json" -Raw | ConvertFrom-Json)) { $before[$e.rel] = $e.hash }
    $aside = "$S\added\$tag"
    $changed = 0; $added = 0
    foreach ($f in Get-ChildItem -LiteralPath $live -Recurse -File -Force) {
        $rel = $f.FullName.Substring($live.Length)
        if ($before.ContainsKey($rel)) {
            if ((Get-FileHash -LiteralPath $f.FullName).Hash -ne $before[$rel]) {
                Copy-Item -LiteralPath (Resolve-RunChild "$S\backup\$tag" $rel) -Destination $f.FullName -Force; $changed++; "restored ($tag): $rel"
            }
        } else {
            $to = Resolve-RunChild $aside $rel; New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
            if (Test-Path -LiteralPath $to) { $to += '.' + [Guid]::NewGuid().ToString('N') }
            $source = Resolve-RunChild $live $rel
            Move-Item -LiteralPath $source -Destination $to -Force; $added++; "moved aside ($tag): $rel"
        }
    }
    foreach ($rel in $before.Keys) {
        $dest = Resolve-RunChild $live $rel
        if (-not (Test-Path -LiteralPath $dest)) {
            New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
            Copy-Item -LiteralPath (Resolve-RunChild "$S\backup\$tag" $rel) -Destination $dest -Force; $changed++; "put back missing ($tag): $rel"
        }
    }
    Get-ChildItem -LiteralPath $live -Recurse -Directory -Force | Sort-Object { $_.FullName.Length } -Descending | Where-Object { -not (Get-ChildItem -LiteralPath $_.FullName -Force) } | ForEach-Object {
        $rel = $_.FullName.Substring($live.Length)
        if (-not (Test-Path -LiteralPath (Resolve-RunChild "$S\backup\$tag" $rel))) { Remove-Item -LiteralPath (Resolve-RunChild $live $rel) -Confirm:$false; "removed empty folder ($tag): $rel" }
    }
    "${tag}: $changed restored, $added moved aside"
}

$bad = 0
foreach ($r in $roots) {
    $live = $r[0]; $tag = $r[1]
    $before = Get-Content "$S\before-$tag.json" -Raw | ConvertFrom-Json
    $now = @{}; foreach ($f in Get-ChildItem -LiteralPath $live -Recurse -File -Force) { $now[$f.FullName.Substring($live.Length)] = (Get-FileHash -LiteralPath $f.FullName).Hash }
    foreach ($e in $before) { if ($now[$e.rel] -ne $e.hash) { $bad++; "DIFFERS ($tag): $($e.rel)" } }
    if ($now.Count -ne @($before).Count) { $bad++; "COUNT ($tag): $($now.Count) now, $(@($before).Count) before" }
}
"verification: $bad differences"
$i = "$G\ME1\Binaries\Win64\ASI\LE1MEVR.ini"
if ((Test-Path $i) -and (Test-Path "$S\backup\ME1ASI\LE1MEVR.ini")) {
    "LE1MEVR.ini as before: $((Get-FileHash $i).Hash -eq (Get-FileHash "$S\backup\ME1ASI\LE1MEVR.ini").Hash)"
}
if ($bad -ne 0) { throw "the stretch is left open: $bad differences (the backup is in $S\backup)" }
Remove-Item -LiteralPath $current -Confirm:$false
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\run-lock.ps1" -Release -Holder $Holder
