# SPDX-License-Identifier: MIT
# Immutable save fixture -> protected automatic Resume -> bind/replay -> restore current user state.
#Requires -Version 7.0
param(
 [ValidateSet('Plan','Freeze','Bind','Run','Recover')][string]$Action='Plan',
 [string]$PackageDirectory='', [string]$SaveFile='', [string]$ProfileTemplate='',
 [string]$RunDirectory='', [string]$GameRoot='', [string]$SteamRoot='',
 [string]$ProfileRoot='', [string]$PythonExe='python', [switch]$Capture,
 [ValidateRange(5,480)][int]$CaptureStartSeconds=45, [ValidateRange(1,120)][int]$CaptureSeconds=90,
 [string]$LocalAppData=[Environment]::GetFolderPath('LocalApplicationData')
)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $PackageDirectory){$PackageDirectory=Join-Path $repo 'scratch\qa-hubs\citadel-recorded-walk'}
$package=[IO.Path]::GetFullPath($PackageDirectory)
$hub=$PSScriptRoot;$kit=Join-Path $repo 'tools\qa-cafeteria'
. "$kit\common.ps1"
. "$kit\state.ps1"
function Invoke-Fixture([string[]]$Arguments){
 $value=& $PythonExe "$hub\save_fixture.py" @Arguments
 if($LASTEXITCODE){throw "Save fixture check failed: $($value -join ' ')"}
 return ($value -join "`n")|ConvertFrom-Json
}
if($Action -eq 'Freeze'){
 if(-not $SaveFile){throw 'Freeze needs the selected original -SaveFile once.'}
 if(-not $ProfileTemplate){
  if(-not $ProfileRoot){$ProfileRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'}
  $ProfileTemplate=Join-Path $ProfileRoot 'Save\ME1\PROFILE\GamerProfile.pcsav'
 }
 # Freeze only writes the private package. It neither loads a save nor alters the player profile.
 Invoke-Fixture @('freeze',$package,$SaveFile,$ProfileTemplate)
 return
}
if($Action -eq 'Recover'){
 if(-not $RunDirectory){throw 'Recover needs the retained -RunDirectory.'}
 $ctx=Read-QaContext $RunDirectory
 $data=Join-Path $ctx.LocalAppData 'MEVR';$lockPath=Join-Path $data 'qa-cafeteria.lock'
 if((Get-Content -LiteralPath $lockPath -Raw) -ne [IO.Path]::GetFullPath($RunDirectory)){throw 'Recovery lease belongs to another run.'}
 $owner=Get-Content -LiteralPath "$RunDirectory\session-owner.json" -Raw|ConvertFrom-Json
 $controller=Get-Process -Id $owner.scriptPid -ErrorAction SilentlyContinue
 if($controller -and $controller.StartTime.ToUniversalTime().Ticks -eq $owner.scriptStartedTicks){throw 'The session controller is still active.'}
 if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Close the game and launcher before recovery.'}
 if((Get-QaRuntime) -ne $ctx.ActiveRuntime){throw 'Runtime changed during session; inspect before recovery.'}
 Restore-QaBackup $RunDirectory
 Remove-Item -LiteralPath $lockPath
 return
}
$fixture=Invoke-Fixture @('verify',$package)
if($Action -eq 'Plan'){
 @{package=$package;fixture=$fixture;startBound=(Test-Path -LiteralPath "$package\start.json");
  workflow='Stage archived save and temporary Resume selection; launch; check Citadel; bind or replay; close owned game; restore current profile/settings.';
  runtime='Uses the currently selected runtime and headset; does not enable the null driver.'}|ConvertTo-Json -Depth 10
 return
}
$routeJson=& $PythonExe "$hub\recorded_route.py" verify $package
if($LASTEXITCODE){throw 'Recording/manifest verification failed before launch.'}
$route=($routeJson -join "`n")|ConvertFrom-Json
if($Capture -and $CaptureStartSeconds+$CaptureSeconds+5 -gt $route.durationSeconds){throw 'Capture window exceeds the recording.'}
if($Action -eq 'Run' -and -not (Test-Path -LiteralPath "$package\start.json")){throw 'Run -Action Bind once to automatically load and bind this archived fixture.'}
if($Action -eq 'Bind' -and (Test-Path -LiteralPath "$package\start.json")){throw 'Start is already bound; use Run.'}
if(Test-Path -LiteralPath "$package\start.json"){
 $bound=Get-Content -LiteralPath "$package\start.json" -Raw|ConvertFrom-Json
 if($bound.saveSha256 -ne $fixture.saveSha256){throw 'Frozen save differs from the bound walk.'}
}
if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Game or launcher already active; close it before this automatic session.'}
if(-not (Get-Process steam -ErrorAction SilentlyContinue)){throw 'Start Steam and sign in first.'}
if(-not $SteamRoot){$SteamRoot=(Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/','\'}
if(-not $GameRoot){$GameRoot=Find-QaInstall $SteamRoot '1328670'}
if(-not $ProfileRoot){$ProfileRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'}
$ProfileRoot=[IO.Path]::GetFullPath($ProfileRoot)
$asi=Join-Path $GameRoot 'Game\ME1\Binaries\Win64\ASI'
$data=Join-Path $LocalAppData 'MEVR'
New-Item -ItemType Directory -Path $data -Force|Out-Null
if(@(Get-ChildItem -LiteralPath $data -Filter '*.request').Count){throw 'Pending test-loop request exists.'}
$lockPath=Join-Path $data 'qa-cafeteria.lock'
$lease=[IO.File]::Open($lockPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$RunDirectory=Join-Path $package ('sessions\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$oldLocal=$env:LOCALAPPDATA;$backupReady=$false;$restored=$false
$receipt=[ordered]@{action=$Action;completed=$false;restored=$false;nativeRouteValidated=$false}
try{
 New-Item -ItemType Directory -Path $RunDirectory|Out-Null
 $bytes=[Text.Encoding]::UTF8.GetBytes($RunDirectory);$lease.Write($bytes,0,$bytes.Length);$lease.Flush()
 $owner=@{kind='citadel-recorded-walk';package=$package;scriptPid=$PID;scriptStartedTicks=(Get-Process -Id $PID).StartTime.ToUniversalTime().Ticks;
          gamePid=0;gameStartedTicks=0;childRun=$null}
 $owner|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\session-owner.json"
 $owner|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\owner.json"
 $env:LOCALAPPDATA=$LocalAppData
 $source=Join-Path $RunDirectory 'source\tools'
 New-Item -ItemType Directory -Path $source|Out-Null
 Copy-Item -LiteralPath $hub,$kit -Destination $source -Recurse
 foreach($name in @('common.ps1','console.ps1','mevrrec.py')){Copy-Item -LiteralPath "$repo\tools\$name" -Destination $source}
 $hub=Join-Path $source 'qa-hubs';$kit=Join-Path $source 'qa-cafeteria'
 $ctx=@{RepoRoot=(Split-Path $source -Parent);GameRoot=$GameRoot;SteamRoot=$SteamRoot;ProfileRoot=$ProfileRoot;LocalAppData=$LocalAppData;
        PythonExe=$PythonExe;ExpectedAsiSha256=(Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash;ActiveRuntime=(Get-QaRuntime)}
 $ctx|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\context.json"
 @{Action=$Action;PackageDirectory=$package;GameRoot=$GameRoot;SteamRoot=$SteamRoot;ProfileRoot=$ProfileRoot;PythonExe=$PythonExe;
   LocalAppData=$LocalAppData;Capture=[bool]$Capture;CaptureStartSeconds=$CaptureStartSeconds;CaptureSeconds=$CaptureSeconds}|
   ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\invocation.json"
 $gameConfig=Join-Path $GameRoot 'Game\ME1\BioGame\Config'
 Save-QaBackup $RunDirectory @("$asi\LE1MEVR.ini","$asi\MELEVR.ini") @($ProfileRoot,$gameConfig) $ctx.ExpectedAsiSha256 $ctx.ActiveRuntime
 $backupReady=$true
 # Recheck after backup; use current preferences, changing only the Resume character/slot in a temporary copy.
 if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Another session opened during preparation.'}
 $fixture=Invoke-Fixture @('verify',$package)
 $liveProfile=Join-Path $ProfileRoot 'Save\ME1\PROFILE\GamerProfile.pcsav'
 $template=if(Test-Path -LiteralPath $liveProfile){$liveProfile}else{"$package\fixture\profile-template.pcsav"}
 [void](Invoke-Fixture @('resume-profile',$package,$template,"$RunDirectory\resume-profile.pcsav"))
 $saveRelative='Save\ME1\'+$fixture.career+'\'+$fixture.slot+'.pcsav'
 $liveSave=[IO.Path]::GetFullPath((Join-Path $ProfileRoot $saveRelative))
 $profilePrefix=$ProfileRoot.TrimEnd('\')+'\'
 if(-not $liveSave.StartsWith($profilePrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Save target escaped the protected profile.'}
 New-Item -ItemType Directory -Path (Split-Path $liveSave -Parent),(Split-Path $liveProfile -Parent) -Force|Out-Null
 Copy-Item -LiteralPath "$package\fixture\save.pcsav" -Destination $liveSave -Force
 Copy-Item -LiteralPath "$RunDirectory\resume-profile.pcsav" -Destination $liveProfile -Force
 if((Get-FileHash -LiteralPath $liveSave).Hash -ne $fixture.saveSha256){throw 'Staged save differs from fixture.'}
 @{savePath=$liveSave;saveSha256=$fixture.saveSha256;profileSha256=(Get-FileHash -LiteralPath $liveProfile).Hash}|
   ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\staged.json"
 & "$kit\launch-ready.ps1" -RunDirectory $RunDirectory -ExpectedMap 'BIOA_STA00'
 $game=Assert-QaOwnedGame
 $owner.gamePid=$game.Id;$owner.gameStartedTicks=$game.StartTime.ToUniversalTime().Ticks
 $owner|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\session-owner.json"
 # Hash still matches the file that was staged; a running game must not have replaced it before admission.
 if((Get-FileHash -LiteralPath $liveSave).Hash -ne $fixture.saveSha256){throw 'Fixture slot changed during startup.'}
 $worldText=Invoke-QaTool 'console.ps1' @{Command=@('mevr vehicle','mevr loc')}
 $worldText|Set-Content -LiteralPath "$RunDirectory\loaded-world.txt"
 $observedJson=& $PythonExe "$hub\recorded_route.py" world "$RunDirectory\loaded-world.txt"
 if($LASTEXITCODE){throw 'Loaded save is not on-foot Citadel gameplay.'}
 $observed=($observedJson -join "`n")|ConvertFrom-Json
 $saved=$fixture.locationCandidates[0].positionUU
 $distance=[Math]::Sqrt([Math]::Pow($observed.positionUU[0]-$saved[0],2)+[Math]::Pow($observed.positionUU[1]-$saved[1],2)+[Math]::Pow($observed.positionUU[2]-$saved[2],2))
 if($distance -gt 150){throw "Loaded position is $distance UU from the archived save; refusing replay/binding."}
 $receipt.loadedPositionDistanceUU=$distance
 $receipt.nativeLoadValidated=$true
 $arguments=@{PackageDirectory=$package;GameProcessId=$game.Id;PythonExe=$PythonExe;LocalAppData=$LocalAppData;SessionDirectory=$RunDirectory}
 if($Action -eq 'Bind'){
  & "$hub\citadel-walk.ps1" @arguments -Action BindStart -SaveFile "$package\fixture\save.pcsav"
 }else{
  & "$hub\citadel-walk.ps1" @arguments -Action Run -Capture:$Capture -CaptureStartSeconds $CaptureStartSeconds -CaptureSeconds $CaptureSeconds
 }
 $receipt.completed=$true
}catch{
 $receipt.error=$_.Exception.Message
 throw
}finally{
 try{
  if($backupReady){
   if(Get-Process MassEffect1 -ErrorAction SilentlyContinue){
    [void](Assert-QaOwnedGame)
    Invoke-QaTool 'console.ps1' @{Command=@('exit')}
    $until=(Get-Date).AddSeconds(45)
    while((Get-Process MassEffect1 -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until){Start-Sleep -Milliseconds 500}
   }
   if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Game/launcher remains open; restore is deferred.'}
   if((Get-QaRuntime) -ne $ctx.ActiveRuntime){throw 'Runtime changed; restore is deferred.'}
   if((Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash -ne $ctx.ExpectedAsiSha256){throw 'Installed build changed; inspect shared-session ownership before restore.'}
   Restore-QaBackup $RunDirectory
   $receipt.restored=$true
  }
  $restored=$true
 }catch{
  $receipt.recoveryError=$_.Exception.Message
  Write-Warning "Automatic session needs recovery: $RunDirectory ($($receipt.recoveryError))"
 }finally{
  if(Test-Path -LiteralPath $RunDirectory){$receipt|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$RunDirectory\session-result.json"}
  $env:LOCALAPPDATA=$oldLocal
  $lease.Dispose()
  if($restored){Remove-Item -LiteralPath $lockPath}
  Write-Host "Automatic session artifacts: $RunDirectory"
 }
}
if(-not $restored){throw 'Profile restoration is pending. Use -Action Recover with the retained run directory after closing the game.'}
