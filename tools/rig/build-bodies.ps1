# SPDX-License-Identifier: MIT
# tools/rig/README.md "Every female body": builds plans/<game>-females.jsonc at Idle priority into
# %LOCALAPPDATA%\MEVR\rig\<GAME>\MEVR_Bodies_<GAME>.pcc (+ .json index, colliders\), its log beside it. Reads the game; writes
# nothing there (installing is tools/rig/install.ps1's, with the owner's yes).
param(
    [Parameter(Mandatory)][ValidateSet('LE1', 'LE2', 'LE3')][string]$Game,
    [string]$Rig = ''
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Rig) { $Rig = Join-Path $here 'bin\out\mevr-rig.exe' }
$plan = Join-Path $here "plans\$($Game.ToLower())-females.jsonc"
$log = Join-Path $env:LOCALAPPDATA "MEVR\rig\$Game\MEVR_Bodies_$Game.log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
$p = Start-Process -FilePath $Rig -ArgumentList @('rig', "`"$plan`"", '--game', $Game) -NoNewWindow -PassThru `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err"
$p.PriorityClass = 'Idle'
$p.WaitForExit()
Get-Content $log -Tail 1
Get-Content "$log.err" -Tail 5
"exit $($p.ExitCode): $log"
