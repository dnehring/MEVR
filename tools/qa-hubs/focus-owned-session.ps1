# SPDX-License-Identifier: MIT
# Observe or foreground the owned game; records the intervention so it can be excluded from frame comparisons.
# Default LE1 retains qa-cafeteria context behavior. -Game LE2 reads qa-le2 context without environment redirection.
# Example: ./focus-owned-session.ps1 -Game LE2 -RunDirectory <owned-run> [-Activate]
# Without -Activate only reads window/process state and appends launch-events.jsonl; no focus or input change.
# LE2 requires the caller's normal desktop LOCALAPPDATA coordination scope; this is process/lock ownership
# verification, not control-channel, loaded-module, frozen-plan or gameplay acceptance. Never launches a game.
# Offline fake Win32/process checks: ./test-focus-owned-session.ps1. Native focus acceptance remains separate.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,[switch]$Activate,
      [ValidateSet('LE1','LE2')][string]$Game='LE1')
$ErrorActionPreference='Stop'
$RunDirectory=[IO.Path]::GetFullPath($RunDirectory)
if($Game -eq 'LE1'){
 . (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
 $ctx=Read-QaContext $RunDirectory
 if((Get-Content -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock') -Raw) -ne $RunDirectory){throw 'A different run owns the game.'}
}else{
 # LE2 context has a different lifecycle. Never use LE1 Read-QaContext: it redirects LOCALAPPDATA.
 function Assert-Le2FocusPath([string]$Path){
  if([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathFullyQualified($Path)){throw 'LE2 focus requires absolute paths.'}
  $full=[IO.Path]::GetFullPath($Path)
  for($part=$full;$part;$part=[IO.Path]::GetDirectoryName($part)){
   if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){
    throw 'LE2 focus refuses reparse paths.'
   }
  }
  return $full
 }
 $RunDirectory=Assert-Le2FocusPath $RunDirectory
 $focusLocal=Assert-Le2FocusPath $env:LOCALAPPDATA
 $focusLock=Assert-Le2FocusPath (Join-Path $focusLocal 'MEVR\qa-cafeteria.lock')
 $focusContext=Assert-Le2FocusPath (Join-Path $RunDirectory 'context.json')
 $focusContextText=Get-Content -LiteralPath $focusContext -Raw
 $ctx=$focusContextText|ConvertFrom-Json
 if($ctx.schemaVersion -ne 1 -or $ctx.runDirectory -cne $RunDirectory -or $ctx.phase -cne 'game-bound' -or
    $ctx.backupComplete -isnot [bool] -or $ctx.backupComplete -ne $true){throw 'LE2 focus requires the exact backed-up game-bound session.'}
 if((Assert-Le2FocusPath $ctx.lockPath) -ne $focusLock){throw 'LE2 focus requires the current environment coordination lock.'}
 $focusExe=Assert-Le2FocusPath $ctx.exePath
 if([IO.Path]::GetFileName($focusExe) -ine 'MassEffect2.exe' -or $ctx.gameProcess.name -cne 'MassEffect2' -or
    (Assert-Le2FocusPath $ctx.gameProcess.path) -ne $focusExe){throw 'LE2 executable binding differs from the session.'}
 function Assert-Le2FocusOwner {
  if((Get-Content -LiteralPath $focusContext -Raw) -cne $focusContextText -or
     (Get-Content -LiteralPath $focusLock -Raw) -cne $RunDirectory -or
     (Test-Path -LiteralPath (Join-Path (Split-Path $focusLock -Parent) 'game-slot\lock.json'))){throw 'LE2 session or coordination ownership changed.'}
  $owned=Get-Process -Id $ctx.gameProcess.pid -ErrorAction Stop
  if($owned.ProcessName -cne 'MassEffect2' -or $owned.Id -ne $ctx.gameProcess.pid -or
     (Assert-Le2FocusPath $owned.Path) -ne $focusExe -or
     $owned.StartTime.ToUniversalTime().Ticks -ne ([datetime]$ctx.gameProcess.started).ToUniversalTime().Ticks){
   throw 'The owned LE2 process ended or changed.'
  }
  return $owned
 }
 [void](Assert-Le2FocusOwner)
}
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
$ownedGame=if($Game -eq 'LE1'){Assert-QaOwnedGame}else{Assert-Le2FocusOwner}
$ownedGame.Refresh()
if($ownedGame.MainWindowHandle -eq [IntPtr]::Zero){throw 'The game window is not ready.'}
$before=[QaStartupFocus]::GetForegroundWindow();[uint32]$beforePid=0
$foregroundThread=[QaStartupFocus]::GetWindowThreadProcessId($before,[ref]$beforePid)
$thisThread=[QaStartupFocus]::GetCurrentThreadId();$attached=$false;$accepted=$false
try{
 if($Activate -and $before -ne $ownedGame.MainWindowHandle){
  if($foregroundThread -ne 0 -and $foregroundThread -ne $thisThread){$attached=[QaStartupFocus]::AttachThreadInput($thisThread,$foregroundThread,$true)}
  $accepted=[QaStartupFocus]::SetForegroundWindow($ownedGame.MainWindowHandle)
 }
}finally{if($attached){[void][QaStartupFocus]::AttachThreadInput($thisThread,$foregroundThread,$false)}}
$after=[QaStartupFocus]::GetForegroundWindow();[uint32]$afterPid=0
[void][QaStartupFocus]::GetWindowThreadProcessId($after,[ref]$afterPid)
$result=@{utc=[datetime]::UtcNow.ToString('o');beginUtc=$begin;kind='foreground-activate';requested=[bool]$Activate;
 gamePid=$ownedGame.Id;beforePid=$beforePid;afterPid=$afterPid;foreground=($after -eq $ownedGame.MainWindowHandle);accepted=$accepted}
$result|ConvertTo-Json -Compress|Add-Content -LiteralPath "$RunDirectory\launch-events.jsonl"
$result|ConvertTo-Json
if($Activate -and -not $result.foreground){throw 'Windows did not foreground the owned game.'}
