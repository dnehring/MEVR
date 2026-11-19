# SPDX-License-Identifier: MIT
# The top bar's Leave (research/28 "Leave"; the owner's answer to question 16, 2026-11-17): with a stretch open (prep.ps1,
# launch.ps1, load.ps1), on foot, this asks the mod whether Leave would show here and why (`mevr omni offers`: each loaded
# SFXSeqEvt_LeaveWorld event, armed or not), opens our menu by its request file, takes a snapshot (the `mainmenu` layer shows the
# top bar), and presses the top bar's Leave by its element id (`press game-leave`): refused while the menu doesn't show it, else
# the host asks the mod and the mod calls the active mode's LeaveWorld once the menu's pause is lifted; then the offers again
# (an event the call reached counts one more trigger). With -Map <persistent level> it first opens that map through the console
# (`open <map>`; BioP_N7Mine carries a leave event in its persistent level) and waits for gameplay.
# Results in the stretch's session folder, leave-<time>\.
#
#   tools\le2-halcyon-run\leave.ps1 -Holder <you> [-Game LE3] [-Map BioP_N7Mine] [-Tag what]
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [ValidatePattern('^[A-Za-z0-9_]*$')][string]$Map = '',
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$Tag = 'here',
    [int]$MapTimeoutSeconds = 150
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
$out = Join-Path $open.session ('leave-' + $Tag + '-' + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$log = Join-Path $bin ("Logs\{0}MEVR.log" -f $Game)
$menuRequest = Join-Path $env:LOCALAPPDATA 'MEVR\mainmenu.request'

function Ask([string]$command, [int]$timeout = 10) {
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\console.ps1') $command -TimeoutSeconds $timeout 2>$null
    $joined = ($text | Where-Object { $_ }) -join "`n"
    Add-Content -LiteralPath (Join-Path $out 'console.txt') -Value "> $command`n$joined`n"
    return $joined
}
function ReadLog {
    $stream = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
    $reader = New-Object IO.StreamReader($stream)
    try { return @($reader.ReadToEnd() -split "`r?`n" | Where-Object { $_ -ne '' }) } finally { $reader.Close() }
}
function MenuRequest([string]$line) {
    Set-Content -LiteralPath $menuRequest -Value $line -Encoding ascii
    $deadline = (Get-Date).AddSeconds(5)
    while ((Test-Path -LiteralPath $menuRequest) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    if (Test-Path -LiteralPath $menuRequest) { throw "the menu request '$line' was not taken" }
}
$verdicts = New-Object System.Collections.Generic.List[string]
function Verdict([string]$what, [bool]$ok, [string]$detail) {
    $line = '{0} {1}: {2}' -f ($(if ($ok) { 'PASS' } else { 'FAIL' }), $what, $detail)
    $verdicts.Add($line)
    Write-Output $line
}

if ($Map) {
    $before = Ask 'mevr state'
    "before: $before"
    $null = Ask "open $Map" 20
    $deadline = (Get-Date).AddSeconds($MapTimeoutSeconds)
    $state = ''
    $onMap = { param($s) $s -match ("world {0}[;.]" -f [regex]::Escape($Map)) }
    do {
        Start-Sleep -Seconds 3
        try { $state = Ask 'mevr state' 5 } catch { $state = '' }
    } until (((& $onMap $state) -and $state -match 'game mode [07]\b') -or (Get-Date) -gt $deadline)
    "after: $state"
    if ((& $onMap $state) -and $state -match 'game mode 7\b' -and $Game -eq 'LE2') {
        # A map the console opens starts with the squad selection: its own A on the confirmation (`mevr omni party`), then the
        # weapon loadout (a ChoiceGUI), left with B as the player would (`mevr omni close`: B until the GUI mode ends).
        Start-Sleep -Seconds 3
        "party: " + (Ask 'mevr omni party')
        foreach ($wait in 1..3) {
            $deadline = (Get-Date).AddSeconds(20)
            do {
                Start-Sleep -Seconds 3
                try { $state = Ask 'mevr state' 5 } catch { $state = '' }
            } until ($state -match 'game mode 0\b' -or (Get-Date) -gt $deadline)
            "after the party ($wait): $state"
            if ($state -match 'game mode 0\b') { break }
            if ($wait -gt 1) { "party again: " + (Ask 'mevr omni party') }  # B on the loadout may have gone back to the squad
            "close: " + (Ask 'mevr omni close')
        }
    }
    Verdict "$Map opened, on foot" ($state -match 'game mode 0\b' -and $state -match [regex]::Escape($Map)) $state
    Start-Sleep -Seconds 5  # its streaming and its script's first events
}

Ask 'mevr state' | Write-Output
$offers = Ask 'mevr omni offers'
"offers: $offers"
$offered = $offers -match '(?m)^leave: offered'  # the reply's lines follow console.ps1's echo of the command
$from = (ReadLog).Count
MenuRequest 'open guide'
Start-Sleep -Seconds 2
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $W 'tools\snapshot.ps1') -Tag ("leave-" + $Tag) | Set-Content -LiteralPath (Join-Path $out 'snapshot.txt') -Encoding utf8
MenuRequest 'press game-leave'
Start-Sleep -Seconds 4
if (-not $offered) { MenuRequest 'close' }
Start-Sleep -Seconds 3
$after = Ask 'mevr omni offers'
"offers after: $after"
$lines = @((ReadLog) | Select-Object -Skip $from | Where-Object { $_ -match 'main menu|leave' })
$lines | Set-Content -LiteralPath (Join-Path $out 'lines.txt') -Encoding utf8
$refused = @($lines | Where-Object { $_ -match 'press game-leave: not shown' }).Count -gt 0
$pressed = @($lines | Where-Object { $_ -match 'pressed game-leave' }).Count -gt 0
$called = @($lines | Where-Object { $_ -match 'main menu: leave: \S+\.LeaveWorld.* called' })
if ($offered) {
    Verdict 'Leave shown and pressed: the mod called LeaveWorld' ($pressed -and $called.Count -gt 0) $(if ($called.Count) { $called[0] } else { "pressed $pressed, no LeaveWorld line" })
} else {
    Verdict 'Leave hidden: the press refused, nothing called' ($refused -and -not $pressed -and $called.Count -eq 0) "refused $refused, pressed $pressed"
}
$verdicts | Set-Content -LiteralPath (Join-Path $out 'verdicts.txt') -Encoding utf8
"results: $out"
