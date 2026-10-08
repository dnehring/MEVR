# Deploys the built plugin into the game's ASI folder.
#
# Hot-reload builds (MEVR_HOT_RELOAD=ON, the default): copies the small host ASI and writes
# <GAME>MEVR.modpath pointing at build/<Config>/<GAME>MEVR_mod.dll. From then on, every
# `cmake --build` is picked up by the running game within about a second; redeploy only when
# the host itself changes (that needs the game closed, since the loaded ASI is locked).
# Monolithic builds: copies the single ASI.
#
# Requires the ME3Tweaks ASI loader (LEBinkProxy), installed via ME3Tweaks Mod Manager.
param(
    [ValidateSet('LE1', 'LE2', 'LE3')] [string]$Game = 'LE1',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')] [string]$Config = 'Release',
    [string]$BuildDir = (Join-Path $PSScriptRoot '..\build'),
    [string]$GameRoot,
    # Removes everything this script deploys instead.
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if (-not $GameRoot) { $GameRoot = (Find-MELEInstall).Root }
$bin = Get-MELEBinDir $GameRoot $Game
$asiDir = Join-Path $bin 'ASI'
$name = "${Game}MEVR"
$modPathFile = Join-Path $asiDir "$name.modpath"

if ($Remove) {
    foreach ($p in (Join-Path $asiDir "$name.asi"), (Join-Path $asiDir "$name.pdb"), $modPathFile) {
        if (Test-Path $p) { Remove-Item $p -Confirm:$false; Write-Output "Removed $p" }
    }
    return
}

if (-not (Test-Path (Join-Path $bin 'bink2w64_original.dll'))) {
    throw "ASI loader not installed in $bin. Install it with ME3Tweaks Mod Manager first."
}

$BuildDir = (Resolve-Path $BuildDir).Path
$hotReload = [bool](Select-String -Path (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^MEVR_HOT_RELOAD:BOOL=ON' -Quiet)
$out = Join-Path $BuildDir $Config
$asi = Join-Path $out "$name.asi"
if (-not (Test-Path $asi)) { throw "Not built: $asi (run: cmake --build build --config $Config --target MEVR-$Game)" }
New-Item -ItemType Directory -Force $asiDir | Out-Null

# Copy the host only when it changed; a running game holds the old one open.
$deployedAsi = Join-Path $asiDir "$name.asi"
$same = (Test-Path $deployedAsi) -and ((Get-FileHash $asi).Hash -eq (Get-FileHash $deployedAsi).Hash)
if ($same) {
    Write-Output "$name.asi unchanged"
} else {
    try {
        Copy-Item $asi $asiDir -Force
        $pdb = Join-Path $out "$name.pdb"
        if (Test-Path $pdb) { Copy-Item $pdb $asiDir -Force }
        Write-Output "Deployed $name.asi ($Config, $(if ($hotReload) { 'hot-reload host' } else { 'monolithic' }))"
    } catch {
        throw "Couldn't replace $deployedAsi; quit the game first (the host ASI is locked while it runs)."
    }
}

if ($hotReload) {
    $mod = Join-Path $out "${name}_mod.dll"
    [IO.File]::WriteAllText($modPathFile, $mod)
    Write-Output "Hot reload: $name.modpath -> $mod"
} elseif (Test-Path $modPathFile) {
    Remove-Item $modPathFile -Confirm:$false
}
# LE1 runs Halcyon's renderer, which reads MELEVR.ini next to our ASI. Install its shipped defaults
# once, with the first-run welcome marked seen: that welcome opens Halcyon's menu, which blocks all
# game input until dismissed with Insert (unreachable from the headset).
if ($Game -eq 'LE1') {
    $ini = Join-Path $asiDir 'MELEVR.ini'
    if (-not (Test-Path $ini)) {
        $defaults = Join-Path $PSScriptRoot '..\src\halcyon\le1\runtime\MELEVR.ini'
        $text = [IO.File]::ReadAllText($defaults) -replace '(?m)^firstRunDone = 0', 'firstRunDone = 1'
        # Upstream ships 0.8 (a quarter of life-size: the world looks four times too big). Shepard's
        # skeleton puts 1 unit at about 1 cm, so half a 65 mm IPD is 3.25 units.
        $text = $text -replace '(?m)^sfr2HalfEyeUU = .*$', 'sfr2HalfEyeUU = 3.250'
        # The menu key: Insert by default, which many keyboards lack; backslash (220) is free on LE1.
        $text = $text -replace '(?m)^menuKey = .*$', 'menuKey = 220'
        [IO.File]::WriteAllText($ini, $text)
        Write-Output "Installed Halcyon's default MELEVR.ini (welcome marked seen, life-size stereo)"
    }
}
Write-Output "Log: $(Join-Path $bin "Logs\$name.log")"
