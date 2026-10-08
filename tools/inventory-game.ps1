# Records the ME LE install (exe versions, sizes, hashes, ASI loader state) to notes/game-inventory.md.
# Run after the Steam download finishes, and again after any game update or mod install.
param(
    [string]$GameRoot
)

$ErrorActionPreference = 'Stop'
$AppId = '1328670'

function Find-GameRoot {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/', '\'
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    $libs = @($steam) + (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' |
        ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' })
    foreach ($lib in $libs | Select-Object -Unique) {
        $manifest = Join-Path $lib "steamapps\appmanifest_$AppId.acf"
        if (Test-Path $manifest) {
            $dir = (Select-String -Path $manifest -Pattern '"installdir"\s+"([^"]+)"').Matches[0].Groups[1].Value
            return [pscustomobject]@{ Root = Join-Path $lib "steamapps\common\$dir"; Manifest = $manifest }
        }
    }
    throw "Steam app $AppId not found in any library"
}

$manifest = $null
if (-not $GameRoot) {
    $found = Find-GameRoot
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
    $lines += "| $g | $($exe.Name) | $($exe.VersionInfo.FileVersion) | $($exe.Length) | ``$hash`` | $loader | $asis |"
}

$lines | Set-Content -Path $out -Encoding utf8
Write-Output "Wrote $((Resolve-Path $out).Path)"
