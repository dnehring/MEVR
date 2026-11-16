# SPDX-License-Identifier: MIT
# The sights' check on ME2 (research/le2-vrik.md "The sights on the sequels, mod side"): a pistol raised to the right eye (the
# dot to the aiming eye, the shots toward what the sight line hits), then the sniper rifle if one is carried (ADS, the scope's
# picture, aimed fire). With a stretch open (prep.ps1, launch.ps1) and Save 4 loaded (load.ps1). Results go to the stretch's
# session folder, sights-<time>\ (the recordings, the snapshots' names, the mod's `sights:`, `hand aim: shot` and `weapon:` lines).
#
#   tools\le2-halcyon-run\sights.ps1 -Holder <you> [-PistolDropM 0.085] [-SniperDropM 0.10]
#
# -PistolDropM / -SniperDropM: how far below the right eye the controller's aim pose is held, the gun's sight height over its
# bore (the `sights: the <gun> has a ... sight: ... N above the bore` line, in metres at 100 units a metre).
param(
    [Parameter(Mandatory)][string]$Holder,
    [double]$PistolDropM = 0.085,
    [double]$SniperDropM = 0.10
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
$current = Join-Path $env:LOCALAPPDATA 'MEVR\sessions\le2-current.json'
if (-not (Test-Path -LiteralPath $current)) { throw 'no ME2 stretch is open: prep.ps1, launch.ps1, load.ps1 first' }
$open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
if ($open.holder -ne $Holder) { throw "the open ME2 stretch is held by '$($open.holder)', not '$Holder'" }
if (-not (Get-Process MassEffect2 -ErrorAction SilentlyContinue)) { throw 'ME2 is not running' }
$out = Join-Path $open.session ('sights-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$log = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE2') 'Logs\LE2MEVR.log'

function Ask([string]$command) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds 10 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function Tool([string]$name, [string[]]$arguments) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W "tools\$name") @arguments
}
function ReadLog {
    # Shared read: the game keeps the log open for writing.
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return ($reader.ReadToEnd() -split "`r?`n") } finally { $reader.Close() }
}
$from = (ReadLog).Count

# 1. The pistol raised to the right eye, a shot every 1.5 s.
$pistol = Join-Path $out 'pistol.mevrrec'
& python (Join-Path $W 'tools\make-ads-recording.py') $pistol 40 90 1.75 $PistolDropM 1.5 0.1 --no-support --no-grip
Tool 'replay.ps1' @('-Path', $pistol, '-Loop', '-Inputs') | Out-Null
Start-Sleep -Seconds 8
Tool 'snapshot.ps1' @('-Count', '1', '-Tag', 'sights-pistol') | Tee-Object -FilePath (Join-Path $out 'snapshot-pistol.txt')
Start-Sleep -Seconds 6
Tool 'replay.ps1' @('-Stop') | Out-Null

# 2. The sniper rifle, if one is carried (the answer lists what is): ADS, the scope's picture, aimed fire.
Ask 'mevr weapon sniper' | Write-Output
Start-Sleep -Seconds 3
$sniper = Join-Path $out 'sniper.mevrrec'
& python (Join-Path $W 'tools\make-ads-recording.py') $sniper 40 90 1.75 $SniperDropM 3 0.1 --no-support --no-grip
Tool 'replay.ps1' @('-Path', $sniper, '-Loop', '-Inputs') | Out-Null
Start-Sleep -Seconds 10
Tool 'snapshot.ps1' @('-Count', '1', '-Tag', 'sights-sniper') | Tee-Object -FilePath (Join-Path $out 'snapshot-sniper.txt')
Start-Sleep -Seconds 8
Tool 'replay.ps1' @('-Stop') | Out-Null
Ask 'mevr weapon pistol' | Out-Null

$lines = ReadLog
$new = if ($lines.Count -gt $from) { $lines[$from..($lines.Count - 1)] } else { @() }
$kept = @($new | Where-Object { $_ -match '\] \[\w+\] (sights:|hand aim: shot|weapon:|scope)' })
$kept | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$kept | Select-Object -First 40
"results: $out"
