# SPDX-License-Identifier: MIT
# B is never the game's melee on foot (the owner, 2026-11-17: B on ME2 and ME3 works as on ME1, squad placement's; game/Melee
# AdjustPad keeps it from the game, gamepad::kFlagNoMeleeButton): with a stretch open (prep.ps1, launch.ps1, load.ps1), on foot,
# a replayed B double-tapped and then held (tools/make-power-recording.py `rb`) while a call trace logs every call whose path
# holds TryMelee: none may come. Then the canary: `[Throwables] meleeButton = 1` in ASI\<GAME>MEVR.ini (B the game's own again,
# as before 2026-11-17) and the same replay: the game's melee exec must show in the trace (else the check can't see B at all);
# the key is put back. Needs no squad and no enemy (ME3's Save 31 on the Normandy is enough: the exec runs and refuses there).
# Results in the stretch's session folder, melee-b-<time>\.
#
#   tools\le2-halcyon-run\melee-b.ps1 -Holder <you> [-Game LE3] [-NoCanary]
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [switch]$NoCanary
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$name = if ($Game -eq 'LE3') { 'ME3' } else { 'ME2' }
$current = Join-Path $env:LOCALAPPDATA ("MEVR\sessions\{0}-current.json" -f $Game.ToLowerInvariant())
if (-not (Test-Path -LiteralPath $current)) { throw "no $name stretch is open: prep.ps1, launch.ps1, load.ps1 first" }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open $name stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process ("MassEffect" + $Game.Substring(2)) -ErrorAction SilentlyContinue)) { throw "$name is not running" }
$out = Join-Path $open.session ('melee-b-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$log = Join-Path $bin ("Logs\{0}MEVR.log" -f $Game)
$ini = Join-Path $bin ("ASI\{0}MEVR.ini" -f $Game)

function Ask([string]$command) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function Tool([string]$tool, [string[]]$arguments) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W "tools\$tool") @arguments
}
function ReadLog {
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    # Whole lines only: the file ends with a newline, and an empty last element counted as a line made a `Skip` of the old
    # count skip the first new line (the canary's `call trace: on for` was missed, 2026-11-17).
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
function SetIni([string]$section, [string]$key, [string]$value) {
    Add-Type -Namespace MevrMeleeB -Name Ini -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern bool WritePrivateProfileString(string s, string k, string v, string f);' -ErrorAction SilentlyContinue
    [void][MevrMeleeB.Ini]::WritePrivateProfileString($section, $key, $value, $ini)
}
$verdicts = New-Object System.Collections.Generic.List[string]
function Verdict([string]$what, [bool]$ok, [string]$detail) {
    $line = '{0} {1}: {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' }), $what, $detail)
    $verdicts.Add($line)
    Write-Output $line
}
# B double-tapped (a regroup on the squad's side) and held 2 s (an arc), the trace on meanwhile; the TryMelee calls it saw.
function Run([string]$tag) {
    $recording = Join-Path $out ($tag + '.mevrrec')
    & python (Join-Path $W 'tools\make-power-recording.py') $recording --seconds 5 'rb:1.0:1.12' 'rb:1.3:1.42' 'rb:2.2:4.2' | Out-Null
    if ($LASTEXITCODE) { throw "the $tag recording failed" }
    $from = (ReadLog).Count
    $null = Ask 'mevr calltrace 8 trymelee'
    Tool 'replay.ps1' @('-Path', $recording, '-Inputs') | Out-Null
    Start-Sleep -Seconds 6
    Tool 'replay.ps1' @('-Stop') | Out-Null
    $lines = @((ReadLog) | Select-Object -Skip $from)
    $lines | Where-Object { $_ -match 'call trace:|melee:|squad place:' } | Set-Content -LiteralPath (Join-Path $out ($tag + '-lines.txt')) -Encoding utf8
    return [pscustomobject]@{
        began = @($lines | Where-Object { $_ -match 'call trace: on for' }).Count -gt 0
        melee = @($lines | Where-Object { $_ -match 'call trace: (script|event)\s+\S*TryMelee' })
    }
}

Ask 'mevr state' | Write-Output
"melee: " + (Ask 'mevr melee')
$kept = Run 'kept'
Verdict "B double-tapped and held on foot: no TryMelee ($name's melee exec)" ($kept.began -and $kept.melee.Count -eq 0) (
    $(if (-not $kept.began) { 'the call trace never began' } elseif ($kept.melee.Count) { "$($kept.melee.Count) call(s): $($kept.melee[0])" } else { 'none' }))
if (-not $NoCanary) {
    $was = [regex]::Match([IO.File]::ReadAllText($ini), '(?im)^[ \t]*meleeButton[ \t]*=[ \t]*([^\s;]+)')
    $wasValue = if ($was.Success) { $was.Groups[1].Value } else { '0' }
    SetIni 'Throwables' 'meleeButton' '1'
    Start-Sleep -Seconds 2
    try {
        $canary = Run 'canary'
    } finally {
        SetIni 'Throwables' 'meleeButton' $wasValue
    }
    Verdict 'the canary: meleeButton = 1 (B the game''s again), the same B: TryMelee in the trace' ($canary.began -and $canary.melee.Count -gt 0) (
        $(if (-not $canary.began) { 'the call trace never began' } elseif ($canary.melee.Count) { "$($canary.melee.Count) call(s): $($canary.melee[0])" } else { 'none: the check cannot see B' }))
    Start-Sleep -Seconds 2
}
"melee: " + (Ask 'mevr melee')
$verdicts | Set-Content -LiteralPath (Join-Path $out 'verdicts.txt') -Encoding utf8
"results: $out"
