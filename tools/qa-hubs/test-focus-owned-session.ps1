# SPDX-License-Identifier: MIT
# Actual focus script, private fixtures and fake Win32/process APIs only. Never focuses a desktop window.
#Requires -Version 7.0
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetTempPath()) ('mevr-focus-test-'+[guid]::NewGuid().ToString('N'))
$run=Join-Path $root 'run';$local=Join-Path $root 'local';$redirect=Join-Path $root 'redirect'
New-Item -ItemType Directory -Path $run,"$local\MEVR","$redirect\MEVR"|Out-Null
$originalLocal=$env:LOCALAPPDATA
Add-Type @'
using System;
public static class QaStartupFocus {
 public static IntPtr Foreground=(IntPtr)55;
 public static int Writes, Attachments;
 public static bool Succeed=true;
 public static IntPtr GetForegroundWindow(){return Foreground;}
 public static uint GetWindowThreadProcessId(IntPtr h,out uint p){p=h==(IntPtr)99?123u:456u;return 9;}
 public static uint GetCurrentThreadId(){return 8;}
 public static bool AttachThreadInput(uint a,uint b,bool attach){Attachments+=attach?1:-1;return true;}
 public static bool SetForegroundWindow(IntPtr h){Writes++;if(Succeed)Foreground=h;return Succeed;}
}
'@
function Get-Process { param($Id,$ErrorAction)
 $global:FocusTestProcessReads++
 if($global:FocusTestChangeOnSecondRead -and $global:FocusTestProcessReads -eq 2){$global:FocusTestProcess.StartTime=$global:FocusTestProcess.StartTime.AddSeconds(1)}
 return $global:FocusTestProcess
}
function Reset-Fixture {
 $env:LOCALAPPDATA=$local
 $global:FocusTestProcessReads=0;$global:FocusTestChangeOnSecondRead=$false
 $global:FocusTestProcess=[pscustomobject]@{Id=123;ProcessName='MassEffect2';Path="$root\MassEffect2.exe";
  StartTime=[datetime]'2026-11-14T01:02:03.1234567Z';MainWindowHandle=[IntPtr]99}
 $global:FocusTestProcess|Add-Member -MemberType ScriptMethod -Name Refresh -Value {}
 $script:fixture=@{schemaVersion=1;runDirectory=$run;phase='game-bound';backupComplete=$true;LocalAppData=$redirect;
  lockPath="$local\MEVR\qa-cafeteria.lock";exePath="$root\MassEffect2.exe";
  gameProcess=@{name='MassEffect2';pid=123;path="$root\MassEffect2.exe";started='2026-11-14T01:02:03.1234567Z'}}
 [IO.File]::WriteAllText("$local\MEVR\qa-cafeteria.lock",$run)
 [IO.File]::WriteAllText("$redirect\MEVR\qa-cafeteria.lock",$run)
 if(Test-Path "$run\launch-events.jsonl"){Remove-Item -LiteralPath "$run\launch-events.jsonl"}
 [QaStartupFocus]::Foreground=[IntPtr]55;[QaStartupFocus]::Writes=0;[QaStartupFocus]::Attachments=0;[QaStartupFocus]::Succeed=$true
}
function Invoke-Fixture([switch]$Activate,[switch]$LE1){
 $fixture|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$run\context.json"
 $argsForScript=@{RunDirectory=$run;Activate=$Activate}
 if(-not $LE1){$argsForScript.Game='LE2'}
 & "$PSScriptRoot\focus-owned-session.ps1" @argsForScript|Out-Null
}
$checks=0
function Check([bool]$Ok,[string]$Name){if(-not $Ok){throw "Failed: $Name"};$script:checks++}
try{
 Reset-Fixture;Invoke-Fixture
 $receipt=Get-Content "$run\launch-events.jsonl" -Raw|ConvertFrom-Json
 Check ($env:LOCALAPPDATA -ceq $local) 'LE2 does not redirect environment from context'
 Check ([QaStartupFocus]::Writes -eq 0 -and -not $receipt.requested -and -not $receipt.foreground) 'default observes only'
 Check ($receipt.gamePid -eq 123 -and $receipt.beforePid -eq 456 -and $receipt.afterPid -eq 456) 'existing receipt identity'
 Reset-Fixture;Invoke-Fixture -Activate
 $receipt=Get-Content "$run\launch-events.jsonl" -Raw|ConvertFrom-Json
 Check ([QaStartupFocus]::Writes -eq 1 -and $receipt.requested -and $receipt.foreground -and $receipt.accepted) 'explicit activation only'
 Check ([QaStartupFocus]::Attachments -eq 0) 'thread input detached'
 Reset-Fixture;[QaStartupFocus]::Foreground=[IntPtr]99;Invoke-Fixture -Activate
 Check ([QaStartupFocus]::Writes -eq 0) 'already focused avoids mutation'
 foreach($bad in @('restored','backup','schema','run','lock-path','lock-owner','name','pid','start','recorded-path','exe','actual-path','actual-name','changed-process','no-window')){
  Reset-Fixture
  switch($bad){
   restored {$fixture.phase='restored'}
   backup {$fixture.backupComplete=$false}
   schema {$fixture.schemaVersion=99}
   run {$fixture.runDirectory="$root\other"}
   lock-path {$fixture.lockPath="$redirect\MEVR\qa-cafeteria.lock"}
   lock-owner {[IO.File]::WriteAllText("$local\MEVR\qa-cafeteria.lock","$root\other")}
   name {$fixture.gameProcess.name='MassEffect1'}
   pid {$global:FocusTestProcess.Id=456}
   start {$global:FocusTestProcess.StartTime=$global:FocusTestProcess.StartTime.AddTicks(1)}
   recorded-path {$fixture.gameProcess.path="$root\other\MassEffect2.exe"}
   exe {$fixture.exePath="$root\MassEffect1.exe"}
   actual-path {$global:FocusTestProcess.Path="$root\other\MassEffect2.exe"}
   actual-name {$global:FocusTestProcess.ProcessName='MassEffect1'}
   changed-process {$global:FocusTestChangeOnSecondRead=$true}
   no-window {$global:FocusTestProcess.MainWindowHandle=[IntPtr]::Zero}
  }
  $caught=$false;try{Invoke-Fixture -Activate}catch{$caught=$true}
  Check ($caught -and [QaStartupFocus]::Writes -eq 0 -and -not (Test-Path "$run\launch-events.jsonl")) "refuse $bad before intervention"
  Check ($env:LOCALAPPDATA -ceq $local) "refused $bad preserves environment"
 }
 Reset-Fixture;[QaStartupFocus]::Succeed=$false
 $caught=$false;try{Invoke-Fixture -Activate}catch{$caught=$true}
 $receipt=Get-Content "$run\launch-events.jsonl" -Raw|ConvertFrom-Json
 Check ($caught -and -not $receipt.accepted -and -not $receipt.foreground -and [QaStartupFocus]::Attachments -eq 0) 'failed activation records and detaches'
 Reset-Fixture
 $fixture=@{LocalAppData=$redirect}
 $global:FocusTestProcess.ProcessName='MassEffect1'
 @{pid=123;started='2026-11-14T01:02:03.1234567Z'}|ConvertTo-Json|Set-Content "$run\owned-process.json"
 Invoke-Fixture -LE1
 Check ($env:LOCALAPPDATA -ceq $redirect -and [QaStartupFocus]::Writes -eq 0) 'default LE1 retains existing context and ownership path'
 "Passed $checks offline focus checks; private fixtures: $root"
}finally{$env:LOCALAPPDATA=$originalLocal}
