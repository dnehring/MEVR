# Copies the built plugin (and its .pdb) into the game's ASI folder.
# Requires the ME3Tweaks ASI loader (LEBinkProxy), installed via ME3Tweaks Mod Manager.
param(
    [ValidateSet('LE1', 'LE2', 'LE3')] [string]$Game = 'LE1',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')] [string]$Config = 'Release',
    [string]$GameRoot,
    # Removes the deployed plugin instead.
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if (-not $GameRoot) { $GameRoot = (Find-MELEInstall).Root }
$bin = Get-MELEBinDir $GameRoot $Game
$asiDir = Join-Path $bin 'ASI'
$name = "${Game}MEVR"

if ($Remove) {
    foreach ($ext in '.asi', '.pdb') {
        $p = Join-Path $asiDir "$name$ext"
        if (Test-Path $p) { Remove-Item $p -Confirm:$false; Write-Output "Removed $p" }
    }
    return
}

if (-not (Test-Path (Join-Path $bin 'bink2w64_original.dll'))) {
    throw "ASI loader not installed in $bin. Install it with ME3Tweaks Mod Manager first."
}

$build = Join-Path $PSScriptRoot "..\build\$Config"
$asi = Join-Path $build "$name.asi"
if (-not (Test-Path $asi)) { throw "Not built: $asi (run: cmake --build build --config $Config --target MEVR-$Game)" }

New-Item -ItemType Directory -Force $asiDir | Out-Null
Copy-Item $asi $asiDir -Force
$pdb = Join-Path $build "$name.pdb"
if (Test-Path $pdb) { Copy-Item $pdb $asiDir -Force }
Write-Output "Deployed $name.asi ($Config) to $asiDir"
Write-Output "Log after launch: $(Join-Path $bin "Logs\$name.log") (or %LOCALAPPDATA%\MEVR\$name.log)"
