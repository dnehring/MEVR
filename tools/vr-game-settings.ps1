# Applies VR-friendly graphics settings to a game's GamerSettings.ini, or restores the original.
#
#   UseVSync=False    Present must not also wait for the desktop monitor's refresh: a second
#                     clock beating against the headset's (research/08 H14).
#   MotionBlur=False  Head rotation reads as camera motion, so every head turn smears.
#   FilmGrain=False   Screen-locked noise; uncomfortable in a headset.
#   DepthOfField=False Blurs whatever the game camera isn't focused on, which in VR is wherever the
#                     player looks; it also fights stereo depth.
#   [HDR] DesiredRange=DynamicRange_SDR
#                     HDR renders to an FP16 scRGB backbuffer whose >1.0 highlights the sRGB eye
#                     swapchains clip. Prior art (Halcyon, research/13) sets it here [verify:
#                     the mod log's backbuffer format should no longer be R16G16B16A16_FLOAT].
#
# Optional: -ResX/-ResY set the game's render size, i.e. each eye's render with Halcyon's stereo
# (LE1). Halcyon's Stereo mode expects a square 3072x3072 (its log flags anything else as
# TARGETCHK MISMATCH): the eyes are about square, and a 16:9 render has much less vertical detail.
# Halcyon reports a larger display to the game, so this may exceed the monitor even windowed.
#
# The first -Apply keeps a copy as GamerSettings.ini.mevr_backup; -Restore puts it back.
# The game must be closed (it rewrites the file on exit).
param(
    [ValidateSet('LE1', 'LE2', 'LE3')] [string]$Game = 'LE1',
    [string]$GameRoot,
    [switch]$Restore,
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

$wanted = [ordered]@{
    'SystemSettings' = [ordered]@{ UseVSync = 'False'; MotionBlur = 'False'; FilmGrain = 'False'; DepthOfField = 'False' }
    'HDR'            = [ordered]@{ DesiredRange = 'DynamicRange_SDR' }
}
if ($ResX -or $ResY) {
    if (-not ($ResX -gt 0 -and $ResY -gt 0)) { throw 'Give both -ResX and -ResY.' }
    $wanted['SystemSettings']['ResX'] = "$ResX"
    $wanted['SystemSettings']['ResY'] = "$ResY"
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
