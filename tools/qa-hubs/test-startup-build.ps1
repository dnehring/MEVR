# SPDX-License-Identifier: MIT
# Temporary host/module deployment around a protected QA batch, with exact restoration.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$CandidateDirectory,[Parameter(Mandatory)][string]$OutputDirectory,
 [Parameter(Mandatory)][string]$PackageDirectory,[string]$PythonExe='python',
 [ValidateSet('Startup','Culling')][string]$Mode='Startup',
 [ValidateSet('Splash','Flight','Transition')][string]$Phase='Transition',[int]$Repeats=2,
 [string]$LocalAppData=[Environment]::GetFolderPath('LocalApplicationData'))
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$repo\tools\qa-cafeteria\common.ps1"
if(@(Get-QaProcesses).Count){throw 'Game/launcher/SteamVR session already active.'}
$data=Join-Path $LocalAppData 'MEVR'
if(Test-Path -LiteralPath "$data\qa-cafeteria.lock"){throw 'A native QA session owns this machine.'}
$steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/','\'
$game=Find-QaInstall $steam '1328670'
$asi=Join-Path $game 'Game\ME1\Binaries\Win64\ASI'
if(Test-Path -LiteralPath "$asi\LE1MEVR.modpath"){throw 'An external module override is active.'}
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh output directory.'}
$files=@(Get-Content -LiteralPath "$CandidateDirectory\build-files.json" -Raw|ConvertFrom-Json)
if(@($files).Count -ne 2 -or @($files.name|Sort-Object -Unique).Count -ne 2 -or @($files|Where-Object name -notin @('LE1MEVR.asi','LE1MEVR_mod.dll')).Count){throw 'Expected exactly one host/module pair.'}
foreach($f in $files){if((Get-FileHash -LiteralPath "$CandidateDirectory\$($f.name)").Hash -ne $f.sha256){throw 'Candidate hash mismatch.'}}
New-Item -ItemType Directory -Path "$out\original"|Out-Null
$lock=[IO.File]::Open("$data\qa-build.lock",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$deployed=$false;$backedUp=$false
Start-Transcript -LiteralPath "$out\deployment.txt"|Out-Null
try{
 $original=@(foreach($f in $files){
  $exists=Test-Path -LiteralPath "$asi\$($f.name)"
  if($exists){Copy-Item -LiteralPath "$asi\$($f.name)" -Destination "$out\original"}
  @{name=$f.name;exists=$exists;sha256=$(if($exists){(Get-FileHash -LiteralPath "$out\original\$($f.name)").Hash}else{$null})}
 })
 $original|ConvertTo-Json|Set-Content "$out\original.json"
 $files|ConvertTo-Json|Set-Content "$out\candidate.json"
 $backedUp=$true
 foreach($f in $files){Copy-Item -LiteralPath "$CandidateDirectory\$($f.name)" -Destination "$asi\$($f.name)" -Force}
 $deployed=$true
 if($Mode -eq 'Startup'){
  & "$PSScriptRoot\citadel-startup.ps1" -PackageDirectory $PackageDirectory -PythonExe $PythonExe -Phase $Phase -Repeats $Repeats -OutputDirectory "$out\batch"
 }else{
  & "$PSScriptRoot\citadel-culling.ps1" -PackageDirectory $PackageDirectory -PythonExe $PythonExe -OutputDirectory "$out\batch"
 }
}finally{
 try{
  if($backedUp){
   if(@(Get-QaProcesses).Count -or (Test-Path -LiteralPath "$data\qa-cafeteria.lock")){throw 'Native session remains active: original pair retained; finish session recovery before restoring binaries.'}
   foreach($f in $original){
    if($f.exists -and (Get-FileHash -LiteralPath "$out\original\$($f.name)").Hash -ne $f.sha256){throw 'Original backup changed.'}
    $live=if(Test-Path -LiteralPath "$asi\$($f.name)"){(Get-FileHash -LiteralPath "$asi\$($f.name)").Hash}else{$null}
    $candidate=($files|Where-Object name -eq $f.name).sha256
    if($live -notin @($candidate,$f.sha256)){throw 'Live binary changed by another owner; refusing to overwrite it.'}
   }
   foreach($f in $original){
    if($f.exists){Copy-Item -LiteralPath "$out\original\$($f.name)" -Destination "$asi\$($f.name)" -Force}
    elseif(Test-Path -LiteralPath "$asi\$($f.name)"){Remove-Item -LiteralPath "$asi\$($f.name)"}
   }
   foreach($f in $original){
    $exists=Test-Path -LiteralPath "$asi\$($f.name)"
    if($exists -ne $f.exists -or ($exists -and (Get-FileHash -LiteralPath "$asi\$($f.name)").Hash -ne $f.sha256)){throw 'Binary restoration failed.'}
   }
   @{restored=$true;at=[datetime]::UtcNow.ToString('o');files=$original}|ConvertTo-Json -Depth 4|Set-Content "$out\restored-build.json"
  }
 }finally{$lock.Dispose();Remove-Item -LiteralPath "$data\qa-build.lock";Stop-Transcript|Out-Null}
}
