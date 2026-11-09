# SPDX-License-Identifier: MIT
# Observe or foreground the owned game; records the intervention so it can be excluded from frame comparisons.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,[switch]$Activate)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$RunDirectory=[IO.Path]::GetFullPath($RunDirectory)
$ctx=Read-QaContext $RunDirectory
if((Get-Content -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock') -Raw) -ne $RunDirectory){throw 'A different run owns the game.'}
$begin=[datetime]::UtcNow.ToString('o')
if(-not ('QaStartupFocus' -as [type])){
 Add-Type @'
using System; using System.Runtime.InteropServices;
public static class QaStartupFocus {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
'@
}
$game=Assert-QaOwnedGame
$game.Refresh()
if($game.MainWindowHandle -eq [IntPtr]::Zero){throw 'The game window is not ready.'}
$before=[QaStartupFocus]::GetForegroundWindow();[uint32]$beforePid=0
$foregroundThread=[QaStartupFocus]::GetWindowThreadProcessId($before,[ref]$beforePid)
$thisThread=[QaStartupFocus]::GetCurrentThreadId();$attached=$false;$accepted=$false
try{
 if($Activate -and $before -ne $game.MainWindowHandle){
  if($foregroundThread -ne 0 -and $foregroundThread -ne $thisThread){$attached=[QaStartupFocus]::AttachThreadInput($thisThread,$foregroundThread,$true)}
  $accepted=[QaStartupFocus]::SetForegroundWindow($game.MainWindowHandle)
 }
}finally{if($attached){[void][QaStartupFocus]::AttachThreadInput($thisThread,$foregroundThread,$false)}}
$after=[QaStartupFocus]::GetForegroundWindow();[uint32]$afterPid=0
[void][QaStartupFocus]::GetWindowThreadProcessId($after,[ref]$afterPid)
$result=@{utc=[datetime]::UtcNow.ToString('o');beginUtc=$begin;kind='foreground-activate';requested=[bool]$Activate;
 gamePid=$game.Id;beforePid=$beforePid;afterPid=$afterPid;foreground=($after -eq $game.MainWindowHandle);accepted=$accepted}
$result|ConvertTo-Json -Compress|Add-Content -LiteralPath "$RunDirectory\launch-events.jsonl"
$result|ConvertTo-Json
if($Activate -and -not $result.foreground){throw 'Windows did not foreground the owned game.'}
