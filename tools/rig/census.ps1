# SPDX-License-Identifier: MIT
# tools/rig/README.md "Every female body": the census of a game's female body meshes (mevr-rig census) at Idle priority, into
# %LOCALAPPDATA%\MEVR\rig\census\<GAME>.csv (the player's game data: local, never committed). Reads the game; writes nothing there.
param(
    [Parameter(Mandatory)][ValidateSet('LE1', 'LE2', 'LE3')][string]$Game,
    [string]$Name = '^(HMF|ASA)_',
    [int]$Threads = 4,
    [string]$Rig = '',
    [string]$GameRoot = $(if ($env:MEVR_GAME_ROOT) { $env:MEVR_GAME_ROOT } else { 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition' })
)
if (-not $Rig) { $Rig = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'bin\out\mevr-rig.exe' }
$folder = @{ LE1 = 'ME1'; LE2 = 'ME2'; LE3 = 'ME3' }[$Game]
$bioGame = Join-Path $GameRoot "Game\$folder\BioGame"
$out = Join-Path $env:LOCALAPPDATA "MEVR\rig\census\$Game.csv"
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
$p = Start-Process -FilePath $Rig -ArgumentList @('census', "`"$bioGame`"", "`"$out`"", '--name', "`"$Name`"", '--threads', $Threads, '--game', $Game) `
    -NoNewWindow -PassThru -RedirectStandardError "$out.err"
$p.PriorityClass = 'Idle'
$p.WaitForExit()
Get-Content "$out.err" -Tail 3
"exit $($p.ExitCode): $out"
