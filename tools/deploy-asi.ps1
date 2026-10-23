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
    $fontsDir = Join-Path $asiDir 'fonts'
    if (Test-Path $fontsDir) { Remove-Item $fontsDir -Recurse -Confirm:$false; Write-Output "Removed $fontsDir" }
    $menuDir = Join-Path $asiDir 'menu'
    if (Test-Path $menuDir) { Remove-Item $menuDir -Recurse -Confirm:$false; Write-Output "Removed $menuDir" }
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

# The calibration panel's fonts (Rajdhani, Titillium Web: SIL OFL 1.1, third_party/fonts), with their
# licences, into ASI\fonts, where src/plugin/Calibration looks; without them it falls back to Bahnschrift.
$fontsDir = Join-Path $asiDir 'fonts'
New-Item -ItemType Directory -Force $fontsDir | Out-Null
Copy-Item (Join-Path $PSScriptRoot '..\third_party\fonts\*') $fontsDir -Force
Write-Output "Fonts: $fontsDir"
# Our VR main menu's document and styles (assets/menu: RmlUi .rml/.rcss, research/28), next to the fonts. The menu
# reloads them within a second of a change; [Menu] assets in <GAME>MEVR.ini can point at the repo's folder instead.
$menuDir = Join-Path $asiDir 'menu'
New-Item -ItemType Directory -Force $menuDir | Out-Null
Copy-Item (Join-Path $PSScriptRoot '..\assets\menu\*') $menuDir -Force
Write-Output "Menu: $menuDir"
# LE1 runs Halcyon's renderer, which reads MELEVR.ini next to our ASI. Install what its release
# package ships (reference/MassEffect1-VR/release/MELEVR.ini): the mode and resolutions only, so
# every other setting is Halcyon's tuned code default. Not src/halcyon/le1/runtime/MELEVR.ini: that
# older starting file pins stale values over the tuned ones (eye separation 0.8 instead of 3.4,
# which makes the world look four times too big; eye swap; VR conversations and cutscenes off).
# Our changes: the first-run welcome marked seen (it opens the menu, which blocks all game input);
# the menu on backslash (220), as many keyboards have no Insert; no F-key profile hotkeys (game
# keys); first person on; no head aim (combatHeadAim, makoHeadAim: the hand aims, through our arm
# IK; the head still drives the view). The resolution comes from tools/vr-game-settings.ps1.
if ($Game -eq 'LE1') {
    $ini = Join-Path $asiDir 'MELEVR.ini'
    if (-not (Test-Path $ini)) {
        # Halcyon keeps every setting per profile slot (Default, Custom 1-3), the menu key included,
        # and a slot missing from the file loads as its code defaults (Insert, head aim on, third
        # person). So all four slots get the same block.
        $slot = @(
            'vrMode = 2',
            'sfr2ResX = 4096', 'sfr2ResY = 4250',
            'monoResX = 3840', 'monoResY = 2160',
            'aerResX = 3072', 'aerResY = 3072',
            'dibrResX = 3072', 'dibrResY = 3072',
            'expectedResX = 0', 'expectedResY = 0',
            'firstRunDone = 1',
            'menuKey = 220',
            'fpEnabled = 1',
            'combatHeadAim = 0', 'makoHeadAim = 0',
            # research/16: presenting the desktop mirror every frame costs a lot while focused, and
            # depth pop (P) costs about 30% while on.
            'mirrorPresentEvery = 2', 'depthPopKeyEnabled = 0',
            # research/30: life-size stereo (each eye from its real place, no convergence shift).
            'sfr2Metric = 1', ''
        )
        $lines = @('; MELE VR config - written by the in-headset menu.', '',
            '[active]', 'profile=Default', '',
            '[hotkeys]', 'enabled=0', 'profile0=112', 'profile1=113', 'profile2=114', 'profile3=115', '')
        foreach ($name in 'Default', 'Custom 1', 'Custom 2', 'Custom 3') { $lines += @("[profile:$name]") + $slot }
        [IO.File]::WriteAllText($ini, ($lines -join "`r`n"))
        Write-Output "Installed Halcyon's release MELEVR.ini (its tuned defaults; welcome seen, menu on backslash)"
    }
    # Every deploy, since Halcyon rewrites the file, in every profile slot: the menu on backslash
    # (Insert is Halcyon's default, and many keyboards lack it) and no head aim. Its F1-F4 profile
    # hotkeys are game keys, so they're off. Everything else in a slot is the user's.
    $text = [IO.File]::ReadAllText($ini)
    $fixed = [regex]::Replace($text, '(?ms)(^\[hotkeys\]\r?\n(?:(?!^\[).)*?^enabled=)1', '${1}0')
    $fixed = $fixed -replace '(?m)^menuKey = \d+', 'menuKey = 220'
    $fixed = $fixed -replace '(?m)^combatHeadAim = 1', 'combatHeadAim = 0'
    $fixed = $fixed -replace '(?m)^makoHeadAim = 1', 'makoHeadAim = 0'
    # The HUD panel's old default swing-round (50 degrees, until 2026-10-21) becomes the new 70, the
    # same rule as the world-locked panels (panelFollowDeg). Any other value is the user's.
    $fixed = $fixed -replace '(?m)^hudPanelFollowDeg = 50\.0\b', 'hudPanelFollowDeg = 70.0'
    # Life-size stereo (research/30) on in every profile slot that doesn't have the key yet: added once, after
    # the slot's header. A slot the player has switched it off in keeps its 0.
    $fixed = [regex]::Replace($fixed, '(?ms)^(\[profile:[^\]\r\n]*\])(\r?\n)((?:(?!^\[).)*)', {
        param($m)
        if ($m.Groups[3].Value -match '(?m)^sfr2Metric\s*=') { return $m.Value }
        return $m.Groups[1].Value + $m.Groups[2].Value + 'sfr2Metric = 1' + $m.Groups[2].Value + $m.Groups[3].Value
    })
    if ($fixed -ne $text) {
        [IO.File]::WriteAllText($ini, $fixed)
        Write-Output "MELEVR.ini: every profile slot on backslash with head aim off and life-size stereo set; F-key profile hotkeys off"
    }
}
Write-Output "Log: $(Join-Path $bin "Logs\$name.log")"
