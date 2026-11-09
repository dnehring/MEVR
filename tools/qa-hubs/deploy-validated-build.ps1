# SPDX-License-Identifier: MIT
# Install a retained host/module pair for manual headset QA, or restore its saved original installation.
#Requires -Version 7.0
param([ValidateSet('Install','Restore')][string]$Action='Install',
 [Parameter(Mandatory)][string]$ReceiptDirectory,[string]$CandidateDirectory='',
 [string]$ExpectedInstalledHostSha256='', [string]$GameRoot='',
 [string]$LocalAppData=[Environment]::GetFolderPath('LocalApplicationData'))
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/','\'
if(-not $GameRoot){$GameRoot=Find-QaInstall $steam '1328670'}
$asi=Join-Path $GameRoot 'Game\ME1\Binaries\Win64\ASI'
$out=[IO.Path]::GetFullPath($ReceiptDirectory)
$names=@('LE1MEVR_mod.dll','LE1MEVR.asi') # Publish the module before its host.
function Assert-Closed {
 if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Close the game and launcher before changing their binaries.'}
 if(Test-Path -LiteralPath "$LocalAppData\MEVR\qa-cafeteria.lock"){throw 'A native QA session owns the game.'}
 if(Test-Path -LiteralPath "$asi\LE1MEVR.modpath"){throw 'An external module override is active; review it before deployment.'}
}
function Fingerprint($path){
 $exists=Test-Path -LiteralPath $path
 @{path=$path;exists=$exists;sha256=$(if($exists){(Get-FileHash -LiteralPath $path).Hash}else{$null})}
}
function Publish($source,$destination){
 $temporary="$destination.$([guid]::NewGuid().ToString('N')).mevr-stage"
 try{
  Copy-Item -LiteralPath $source -Destination $temporary
  if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $temporary).Hash){throw 'Staged binary hash mismatch.'}
  [IO.File]::Move($temporary,$destination,$true) # Same-volume replacement; no nullable backup-path binding.
 }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}
function Restore-Original($record){
 foreach($name in $names){
  $saved=$record.original|Where-Object name -eq $name
  $candidate=$record.candidate|Where-Object name -eq $name
  $current=Fingerprint "$asi\$name"
  if($current.sha256 -notin @($saved.sha256,$candidate.sha256)){throw 'Installed binary changed after deployment; refusing to overwrite another build.'}
  if($saved.exists -and (Get-FileHash -LiteralPath "$out\original\$name").Hash -ne $saved.sha256){throw 'Original backup hash mismatch.'}
 }
 # Restore the host first so a monolithic installation no longer needs the module being removed.
 foreach($name in @('LE1MEVR.asi','LE1MEVR_mod.dll')){
  $saved=$record.original|Where-Object name -eq $name
  if($saved.exists){Publish "$out\original\$name" "$asi\$name"}
  elseif(Test-Path -LiteralPath "$asi\$name"){Remove-Item -LiteralPath "$asi\$name"}
 }
 foreach($saved in $record.original){$current=Fingerprint "$asi\$($saved.name)";if($current.exists -ne $saved.exists -or $current.sha256 -ne $saved.sha256){throw 'Original binary restoration failed.'}}
}
Assert-Closed
$lock=[IO.File]::Open("$LocalAppData\MEVR\qa-build.lock",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$record=$null;$mutated=$false
try{
 Assert-Closed
 if($Action -eq 'Restore'){
  $record=Get-Content -LiteralPath "$out\deployment.json" -Raw|ConvertFrom-Json
  if([IO.Path]::GetFullPath($record.asiDirectory) -ne [IO.Path]::GetFullPath($asi)){throw 'Restoration receipt belongs to another installation.'}
  Restore-Original $record
  @{restored=$true;at=[datetime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath "$out\restored.json"
  return
 }
 if(-not $CandidateDirectory -or -not $ExpectedInstalledHostSha256){throw 'Install requires the retained candidate and reviewed current host hash.'}
 if(Test-Path -LiteralPath $out){throw 'Use a fresh receipt directory.'}
 $files=@(Get-Content -LiteralPath "$CandidateDirectory\build-files.json" -Raw|ConvertFrom-Json)
 if($files.Count -ne 2 -or @($files.name|Sort-Object -Unique).Count -ne 2 -or @($files|Where-Object name -notin $names).Count){throw 'Expected exactly one retained host/module pair.'}
 foreach($file in $files){if((Get-FileHash -LiteralPath "$CandidateDirectory\$($file.name)").Hash -ne $file.sha256){throw 'Candidate bytes differ from their manifest.'}}
 if((Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash -ne $ExpectedInstalledHostSha256){throw 'Installed host changed since review.'}
 New-Item -ItemType Directory -Path "$out\original"|Out-Null
 $original=@(foreach($name in $names){$f=Fingerprint "$asi\$name";if($f.exists){Copy-Item -LiteralPath $f.path -Destination "$out\original\$name"};@{name=$name;exists=$f.exists;sha256=$f.sha256}})
 foreach($file in $original){if($file.exists -and (Get-FileHash -LiteralPath "$out\original\$($file.name)").Hash -ne $file.sha256){throw 'Original backup verification failed before installation.'}}
 $protected=@("$asi\LE1MEVR.ini","$asi\MELEVR.ini","$GameRoot\Game\ME1\BioGame\Config\GamerSettings.ini",
  "$GameRoot\Game\ME1\BioGame\Config\BIOEngine.ini","$steam\config\steamvr.vrsettings","$steam\config\chaperone_info.vrchap")
 $settings=@($protected|ForEach-Object {Fingerprint $_})
 $runtime=Get-QaRuntime
 $record=@{asiDirectory=$asi;candidateDirectory=[IO.Path]::GetFullPath($CandidateDirectory);candidate=$files;original=$original;settings=$settings;runtime=$runtime;at=[datetime]::UtcNow.ToString('o')}
 $record|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$out\deployment.json"
 Assert-Closed
 $mutated=$true
 foreach($name in $names){Publish "$CandidateDirectory\$name" "$asi\$name"}
 foreach($file in $files){if((Get-FileHash -LiteralPath "$asi\$($file.name)").Hash -ne $file.sha256){throw 'Installed binary verification failed.'}}
 foreach($saved in $settings){$now=Fingerprint $saved.path;if($now.exists -ne $saved.exists -or $now.sha256 -ne $saved.sha256){throw 'A protected setting changed during deployment.'}}
 if((Get-QaRuntime) -ne $runtime){throw 'Active runtime changed during deployment.'}
 @{installed=$true;settingsPreserved=$true;runtime=$runtime;files=$files;at=[datetime]::UtcNow.ToString('o')}|
  ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$out\installed.json"
 Write-Host 'Retained candidate installed and verified; settings and runtime preserved.'
}catch{
 if($mutated){Assert-Closed;Restore-Original $record}
 throw
}finally{$lock.Dispose();Remove-Item -LiteralPath "$LocalAppData\MEVR\qa-build.lock"}
