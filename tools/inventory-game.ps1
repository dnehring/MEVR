# Records the ME LE install (exe versions, sizes, hashes, ASI loader state) to notes/game-inventory.md.
# Run after the Steam download finishes, and again after any game update or mod install.
param(
    [string]$GameRoot
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$manifest = $null
if (-not $GameRoot) {
    $found = Find-MELEInstall
    $GameRoot = $found.Root
    $manifest = $found.Manifest
}

$out = Join-Path $PSScriptRoot '..\notes\game-inventory.md'
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null

$lines = @("# ME LE install inventory", "", "Generated $(Get-Date -Format 'yyyy-MM-dd HH:mm')", "", "Root: ``$GameRoot``", "")

if ($manifest) {
    $state = (Select-String -Path $manifest -Pattern '"StateFlags"\s+"(\d+)"').Matches[0].Groups[1].Value
    $build = (Select-String -Path $manifest -Pattern '"buildid"\s+"(\d+)"').Matches[0].Groups[1].Value
    $complete = if ($state -eq '4') { 'yes' } else { "no (StateFlags=$state)" }
    $lines += "Steam build: $build. Fully installed: $complete", ""
}

$lines += "| Game | Exe | FileVersion | Size (bytes) | SHA256 | ASI loader | ASI dir |"
$lines += "|------|-----|-------------|--------------|--------|------------|---------|"

foreach ($g in 'ME1', 'ME2', 'ME3') {
    $bin = Join-Path $GameRoot "Game\$g\Binaries\Win64"
    $exe = Get-ChildItem $bin -Filter 'MassEffect*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $exe) { $lines += "| $g | (missing) | | | | | |"; continue }
    $hash = (Get-FileHash $exe.FullName -Algorithm SHA256).Hash
    $loader = if (Test-Path (Join-Path $bin 'bink2w64_original.dll')) { 'installed' } else { 'no' }
    $asiDir = Join-Path $bin 'ASI'
    $asis = if (Test-Path $asiDir) { (Get-ChildItem $asiDir -Filter '*.asi' | ForEach-Object Name) -join ', ' } else { '' }
    if (-not $asis) { $asis = '(none)' }
    # The FileVersion string reads "2.0.0.0"; the build number (e.g. 48602) only lives in the numeric parts.
    $vi = $exe.VersionInfo
    $ver = "$($vi.FileMajorPart).$($vi.FileMinorPart).$($vi.FileBuildPart).$($vi.FilePrivatePart)"
    $lines += "| $g | $($exe.Name) | $ver | $($exe.Length) | ``$hash`` | $loader | $asis |"
}

$lines | Set-Content -Path $out -Encoding utf8
Write-Output "Wrote $((Resolve-Path $out).Path)"
