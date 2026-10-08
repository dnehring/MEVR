# Applies VR-friendly graphics settings to a game's GamerSettings.ini, or restores the original.
#
#   UseVSync=False    Present must not also wait for the desktop monitor's refresh: a second
#                     clock beating against the headset's (research/08 H14).
#   MotionBlur=False  Head rotation reads as camera motion, so every head turn smears.
#   FilmGrain=False   Screen-locked noise; uncomfortable in a headset.
#   [HDR] DesiredRange=DynamicRange_SDR
#                     HDR renders to an FP16 scRGB backbuffer whose >1.0 highlights the sRGB eye
#                     swapchains clip. Prior art (Halcyon, research/13) sets it here [verify:
#                     the mod log's backbuffer format should no longer be R16G16B16A16_FLOAT].
# Depth of field is left alone: Halcyon's installer stopped forcing it off because the game then
# loses colour and looks washed out (tested in ME1 and ME2).
#
# LE1 also gets what Halcyon's installer (MELE-VR.bat) sets for its Stereo mode, which renders
# the whole frame once per eye at a near-square size (height = width / 0.964, the eye's shape):
#   ResX/ResY         the stereo target from -Quality (or -ResX/-ResY). Halcyon reports a display of
#                     that size to the game, so it may exceed the monitor.
#   BorderlessWindow=True, Fullscreen=False
#                     A bordered window is capped by Windows at the real monitor's height: on a
#                     1440p monitor a 2048x2048 target rendered gameplay at 2009x1421, and
#                     Halcyon's left-eye capture (which needs the scene to fill the backbuffer)
#                     never fired, so both eyes got the same image (2026-10-08).
#   DynamicShadows=False  Halcyon's default; the scene renders twice per frame.
#   BIOEngine.ini SmoothFrameRate=FALSE  otherwise the game caps itself at 60 fps.
# It also writes the target into Halcyon's MELEVR.ini (active profile: sfr2ResX/Y, which its
# TARGETCHK log line compares with the live backbuffer, and expectedResX/Y, its watchdog).
#
# The first apply keeps GamerSettings.ini.mevr_backup (and BIOEngine.ini.mevr_backup); -Restore
# puts them back. The game must be closed (it rewrites the file on exit).
param(
    [ValidateSet('LE1', 'LE2', 'LE3')] [string]$Game = 'LE1',
    [string]$GameRoot,
    [switch]$Restore,
    # LE1 stereo target, Halcyon's ladder: Low 2048x2124, Performance 3072x3188 (about the Quest's
    # native eye size), Balanced 4096x4250 (Halcyon's recommendation: supersampled), Sharp 5120x5312,
    # Max 6144x6374.
    [ValidateSet('Low', 'Performance', 'Balanced', 'Sharp', 'Max')] [string]$Quality = 'Performance',
    [int]$ResX,
    [int]$ResY,
    # Prints the current values without changing anything.
    [switch]$Show
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

if (-not $GameRoot) { $GameRoot = (Find-MELEInstall).Root }
$n = $Game.Substring(2)
$ini = Join-Path $GameRoot "Game\ME$n\BioGame\Config\GamerSettings.ini"
$backup = "$ini.mevr_backup"
if (-not (Test-Path $ini)) { throw "Not found: $ini (start the game once to create it)" }

$engineIni = Join-Path $GameRoot "Game\ME$n\BioGame\Config\BIOEngine.ini"
$engineBackup = "$engineIni.mevr_backup"
$halcyonIni = Join-Path $GameRoot "Game\ME$n\Binaries\Win64\ASI\MELEVR.ini"

$wanted = [ordered]@{
    'SystemSettings' = [ordered]@{ UseVSync = 'False'; MotionBlur = 'False'; FilmGrain = 'False' }
    'HDR'            = [ordered]@{ DesiredRange = 'DynamicRange_SDR' }
}
if ($ResX -or $ResY) {
    if (-not ($ResX -gt 0 -and $ResY -gt 0)) { throw 'Give both -ResX and -ResY.' }
} elseif ($Game -eq 'LE1') {
    $ladder = @{ Low = 2048; Performance = 3072; Balanced = 4096; Sharp = 5120; Max = 6144 }
    $ResX = $ladder[$Quality]
    # Halcyon's EyeHeightForWidth (vr_config.h): width / 0.964, rounded, made even.
    $ResY = [int][math]::Floor($ResX / 0.964 + 0.5)
    if ($ResY % 2) { $ResY++ }
}
if ($ResX -gt 0) {
    $wanted['SystemSettings']['ResX'] = "$ResX"
    $wanted['SystemSettings']['ResY'] = "$ResY"
}
if ($Game -eq 'LE1') {
    $wanted['SystemSettings']['BorderlessWindow'] = 'True'
    $wanted['SystemSettings']['Fullscreen'] = 'False'
    $wanted['SystemSettings']['DynamicShadows'] = 'False'
}

function Show-Values([string]$Text) {
    foreach ($section in $wanted.Keys) {
        foreach ($key in $wanted[$section].Keys) {
            $m = [regex]::Match($Text, "(?ms)^\[$section\]\r?\n(?:(?!^\[).)*?^$key=([^\r\n]*)")
            $value = if ($m.Success) { $m.Groups[1].Value } else { '(not set)' }
            Write-Output ("  [{0}] {1}={2}" -f $section, $key, $value)
        }
    }
}

# Sets key=value inside [section], adding the key or the section when missing.
function Set-IniValue([string]$Text, [string]$Section, [string]$Key, [string]$Value) {
    # The header line including its line break, so new keys go on a line of their own.
    $header = [regex]::Match($Text, "(?m)^\[$([regex]::Escape($Section))\][ \t]*(\r?\n|\z)")
    if (-not $header.Success) {
        return $Text.TrimEnd() + "`r`n`r`n[$Section]`r`n$Key=$Value`r`n"
    }
    if ($header.Groups[1].Length -eq 0) {
        return $Text + "`r`n$Key=$Value`r`n"  # header on the last line, nothing after it
    }
    $bodyStart = $header.Index + $header.Length
    $next = [regex]::Match($Text.Substring($bodyStart), '(?m)^\[')
    $bodyEnd = if ($next.Success) { $bodyStart + $next.Index } else { $Text.Length }
    $body = $Text.Substring($bodyStart, $bodyEnd - $bodyStart)
    $keyPattern = "(?m)^$([regex]::Escape($Key))=[^\r\n]*"
    if ([regex]::IsMatch($body, $keyPattern)) {
        $body = [regex]::Replace($body, $keyPattern, "$Key=$Value")
    } else {
        $body = "$Key=$Value`r`n" + $body
    }
    return $Text.Substring(0, $bodyStart) + $body + $Text.Substring($bodyEnd)
}

$text = [IO.File]::ReadAllText($ini)
if ($Show) {
    Write-Output "$ini"
    Show-Values $text
    Write-Output ("  backup: {0}" -f $(if (Test-Path $backup) { $backup } else { 'none' }))
    return
}

if (Get-Process -Name "MassEffect$n" -ErrorAction SilentlyContinue) {
    throw "Close Mass Effect $n first; it rewrites $ini on exit."
}

if ($Restore) {
    if (-not (Test-Path $backup)) { throw "No backup at $backup; nothing to restore." }
    Copy-Item $backup $ini -Force
    Remove-Item $backup -Confirm:$false
    Write-Output "Restored $ini from its backup."
    Show-Values ([IO.File]::ReadAllText($ini))
    if (Test-Path $engineBackup) {
        Copy-Item $engineBackup $engineIni -Force
        Remove-Item $engineBackup -Confirm:$false
        Write-Output "Restored $engineIni from its backup."
    }
    return
}

if (-not (Test-Path $backup)) {
    Copy-Item $ini $backup
    Write-Output "Backed up the original to $backup"
}
foreach ($section in $wanted.Keys) {
    foreach ($key in $wanted[$section].Keys) {
        $text = Set-IniValue $text $section $key $wanted[$section][$key]
    }
}
[IO.File]::WriteAllText($ini, $text)
Write-Output "Applied VR settings to $ini"
Show-Values $text

if ($Game -eq 'LE1') {
    # Frame-rate smoothing: one key, wherever BIOEngine.ini has it (as Halcyon's installer does).
    if (Test-Path $engineIni) {
        $engine = [IO.File]::ReadAllText($engineIni)
        if ($engine -match '(?m)^SmoothFrameRate=TRUE') {
            if (-not (Test-Path $engineBackup)) { Copy-Item $engineIni $engineBackup }
            [IO.File]::WriteAllText($engineIni, [regex]::Replace($engine, '(?m)^SmoothFrameRate=\w+', 'SmoothFrameRate=FALSE'))
            Write-Output "  BIOEngine.ini: SmoothFrameRate=FALSE (was TRUE: a 60 fps cap)"
        } else {
            Write-Output "  BIOEngine.ini: SmoothFrameRate already off"
        }
    }
    # Halcyon's own copy of the target, in every profile slot (it keeps settings per slot, so a slot
    # with another target would flag a mismatch once loaded).
    if ($ResX -gt 0 -and (Test-Path $halcyonIni)) {
        $h = [IO.File]::ReadAllText($halcyonIni)
        foreach ($kv in @(@('sfr2ResX', $ResX), @('sfr2ResY', $ResY), @('expectedResX', $ResX), @('expectedResY', $ResY))) {
            $h = [regex]::Replace($h, "(?m)^$($kv[0]) = [^\r\n]*", "$($kv[0]) = $($kv[1])")
        }
        [IO.File]::WriteAllText($halcyonIni, $h)
        Write-Output "  MELEVR.ini, every profile: sfr2ResX/Y and expectedResX/Y = ${ResX}x${ResY}"
    }
}
