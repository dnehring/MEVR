# SPDX-License-Identifier: MIT
# tools/rig/README.md "Every female body" and "The players' build": `mevr-rig bodies` at Idle priority, everything a game's
# rigged bodies need in one run (plans/players.jsonc: the game's females plan, the breasts' limits against ME3 Liara's romance
# body, the clubs' copies from the plan's "clubs", then Miranda's hair plan on ME2 and ME3) into
# %LOCALAPPDATA%\MEVR\rig\<GAME> (MEVR_Bodies_<GAME>.pcc + .json index, colliders\, MEVR_Miranda.pcc, bodies.json), its log
# beside it. The same command MEVR Setup runs on a player's PC. Reads the game; writes nothing there (installing is
# tools/rig/install.ps1's, with the owner's yes). ME1 and ME2 use ME3's package for the breasts' limits when it's built here
# (else the tool builds ME3's reference body by itself).
param(
    [Parameter(Mandatory)][ValidateSet('LE1', 'LE2', 'LE3')][string]$Game,
    [string]$Rig = '',
    [string]$Out = ''
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Rig) { $Rig = Join-Path $here 'bin\out\mevr-rig.exe' }
if (-not $Out) { $Out = Join-Path $env:LOCALAPPDATA "MEVR\rig\$Game" }
$log = Join-Path $Out "MEVR_Bodies_$Game.log"
New-Item -ItemType Directory -Force $Out | Out-Null
$arguments = @('bodies', '--game', $Game, '--out', "`"$Out`"")
$ref = Join-Path $env:LOCALAPPDATA 'MEVR\rig\LE3\MEVR_Bodies_LE3.pcc'
if ($Game -ne 'LE3' -and (Test-Path $ref)) { $arguments += @('--ref', "`"$ref`"") }
$p = Start-Process -FilePath $Rig -ArgumentList $arguments -NoNewWindow -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$null = $p.Handle
$p.PriorityClass = 'Idle'
$p.WaitForExit()
Get-Content $log | Where-Object { $_ -notmatch '^progress ' } | Select-Object -Last 3
Get-Content "$log.err" -Tail 5
"exit $($p.ExitCode): $log"
