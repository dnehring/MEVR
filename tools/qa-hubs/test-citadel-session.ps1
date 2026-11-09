# SPDX-License-Identifier: MIT
# Runs the real session coordinator against synthetic files and a mocked launch/exit adapter.
#Requires -Version 7.0
param([string]$PythonExe='python')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root=Join-Path $repo ('scratch\qa-hubs\session-offline-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$source=Join-Path $root 'source\tools'
New-Item -ItemType Directory -Path "$source\qa-hubs","$source\qa-cafeteria","$root\profile\Save\ME1\PROFILE",
 "$root\game\Game\ME1\Binaries\Win64\ASI","$root\game\Game\ME1\BioGame\Config","$root\local\MEVR"|Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\citadel-session.ps1","$PSScriptRoot\save_fixture.py","$PSScriptRoot\recorded_route.py" -Destination "$source\qa-hubs"
Copy-Item -LiteralPath "$repo\tools\qa-cafeteria\common.ps1","$repo\tools\qa-cafeteria\state.ps1" -Destination "$source\qa-cafeteria"
foreach($name in @('common.ps1','mevrrec.py','fake-headset.ps1','replay.ps1','snapshot.ps1','make-walk-recording.py','perf-probe.py')){
 Copy-Item -LiteralPath "$repo\tools\$name" -Destination $source
}
& $PythonExe "$PSScriptRoot\test-save-fixture.py" --make "$root\input"
if($LASTEXITCODE){throw 'Synthetic fixture creation failed'}
$package=Join-Path $root 'input\package'
Copy-Item -LiteralPath "$root\input\profile.pcsav" -Destination "$root\profile\Save\ME1\PROFILE\GamerProfile.pcsav"
'current settings'|Set-Content -LiteralPath "$root\game\Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini"
'test binary'|Set-Content -LiteralPath "$root\game\Game\ME1\Binaries\Win64\ASI\LE1MEVR.asi"
$global:SessionMock=@{running=$false;failLaunch=$false;failWalk=$false;root=$root;launches=0;closed=0}
@'
param($RunDirectory,$ExpectedMap)
$global:SessionMock.launches++
$global:SessionMock.running=$true
@{pid=424243;started='2026-11-08T01:00:00Z'}|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\owned-process.json"
# A real startup can autosave and rewrite configuration even before readiness fails.
'generated autosave'|Set-Content -LiteralPath "$($global:SessionMock.root)\profile\Save\ME1\generated.pcsav"
'game changed settings'|Set-Content -LiteralPath "$($global:SessionMock.root)\game\Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini"
if($global:SessionMock.failLaunch){throw 'Mock launch failed after process creation'}
'READY: BIOA_STA00 gameplay'
'@|Set-Content -LiteralPath "$source\qa-cafeteria\launch-ready.ps1"
@'
param($Command)
if($Command[0] -eq 'mevr vehicle'){'map BIOA_STA00, game mode 0, pawn X (BioVehicleBase: no)';'loc 1 2 3 rot 0 0 0';return}
if($Command[0] -ne 'exit'){throw 'Unexpected console command'}
$global:SessionMock.running=$false;$global:SessionMock.closed++
'exit accepted'
'@|Set-Content -LiteralPath "$source\console.ps1"
@'
param($Action,$PackageDirectory,$GameProcessId,$PythonExe,$LocalAppData,$SessionDirectory,$SaveFile,[switch]$Capture,$CaptureStartSeconds,$CaptureSeconds)
if($global:SessionMock.failWalk){throw 'Mock walk failed'}
$fixture=Get-Content -LiteralPath "$PackageDirectory\fixture\fixture.json" -Raw|ConvertFrom-Json
if($Action -eq 'BindStart'){@{saveSha256=$fixture.saveSha256}|ConvertTo-Json|Set-Content -LiteralPath "$PackageDirectory\start.json"}
'Mock walk returned'
'@|Set-Content -LiteralPath "$source\qa-hubs\citadel-walk.ps1"
function Get-Process {
 param([string[]]$Name,[int]$Id)
 if($Id -eq $PID){return Microsoft.PowerShell.Management\Get-Process -Id $PID}
 if($Id -eq 424243 -or ($global:SessionMock.running -and $Name -contains 'MassEffect1')){
  return [pscustomobject]@{Id=424243;ProcessName='MassEffect1';StartTime=[datetime]'2026-11-08T01:00:00Z'}
 }
 if($Name -contains 'steam'){return [pscustomobject]@{Id=123;ProcessName='steam'}}
}
function Get-ItemPropertyValue {param($Path,$Name) 'mock-runtime'}
$arguments=@{PackageDirectory=$package;GameRoot="$root\game";SteamRoot="$root\steam";ProfileRoot="$root\profile";PythonExe=$PythonExe;LocalAppData="$root\local"}
$runner=Join-Path $source 'qa-hubs\citadel-session.ps1'
try{
 foreach($action in @('Bind','Run','FailedLaunch','FailedWalk')){
  if($action -eq 'FailedLaunch'){
   New-Item -ItemType Directory -Path "$root\profile\Save\ME1\Tester00" -Force|Out-Null
   'newer player progress'|Set-Content -LiteralPath "$root\profile\Save\ME1\Tester00\Tester00_96.pcsav"
  }
  $global:SessionMock.failLaunch=$action -eq 'FailedLaunch';$global:SessionMock.failWalk=$action -eq 'FailedWalk'
  $failed=$false
  try{& $runner @arguments -Action $(if($action -eq 'Bind'){'Bind'}else{'Run'})}catch{$failed=$true}
  if($failed -ne ($action -like 'Failed*')){throw "Unexpected session outcome: $action"}
  if($global:SessionMock.running){throw 'Owned process was not closed'}
  if((Get-FileHash -LiteralPath "$root\profile\Save\ME1\PROFILE\GamerProfile.pcsav").Hash -ne
     (Get-FileHash -LiteralPath "$root\input\profile.pcsav").Hash){throw 'Current user profile was not restored'}
  if((Get-Content -LiteralPath "$root\game\Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini" -Raw).Trim() -ne 'current settings'){throw 'Settings were not restored'}
  if(Test-Path -LiteralPath "$root\profile\Save\ME1\generated.pcsav"){throw 'Generated autosave remained'}
  $currentSlot=Join-Path $root 'profile\Save\ME1\Tester00\Tester00_96.pcsav'
  if($action -like 'Failed*'){
   if((Get-Content -LiteralPath $currentSlot -Raw).Trim() -ne 'newer player progress'){throw 'Newer player progress was not restored'}
  }elseif(Test-Path -LiteralPath $currentSlot){throw 'Temporary fixture slot remained'}
  if(Test-Path -LiteralPath "$root\local\MEVR\qa-cafeteria.lock"){throw 'Lease remained after successful restoration'}
 }
 if($global:SessionMock.closed -ne 4){throw 'Not all four owned sessions closed'}
}finally{Remove-Variable SessionMock -Scope Global -ErrorAction SilentlyContinue}
Write-Host "Automatic session lifecycle checks passed: $root"
