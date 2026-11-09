# SPDX-License-Identifier: MIT
# Exercise binary replacement/restoration with synthetic files and a mocked native batch.
#Requires -Version 7.0
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root=Join-Path $repo ('scratch\qa-hubs\build-restoration-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$global:ComparisonTestGame="$root\game";$global:ComparisonTestFail=$false
$asi="$root\game\Game\ME1\Binaries\Win64\ASI"
New-Item -ItemType Directory -Path $asi,"$root\source\tools\qa-hubs","$root\source\tools\qa-cafeteria","$root\local\MEVR","$root\candidate"|Out-Null
Copy-Item "$PSScriptRoot\test-startup-build.ps1" "$root\source\tools\qa-hubs"
@'
function Get-QaProcesses { @() }
function Find-QaInstall { param($SteamRoot,$AppId) $global:ComparisonTestGame }
'@|Set-Content "$root\source\tools\qa-cafeteria\common.ps1"
@'
param($PackageDirectory,$PythonExe,$Phase,$Repeats,$OutputDirectory)
if(-not (Test-Path "$global:ComparisonTestGame\Game\ME1\Binaries\Win64\ASI\LE1MEVR_mod.dll")){throw 'Candidate module missing'}
if($global:ComparisonTestFail){throw 'Expected mock native failure'}
'@|Set-Content "$root\source\tools\qa-hubs\citadel-startup.ps1"
function Get-ItemProperty { param($Path) @{SteamPath='fixture'} }
foreach($name in 'LE1MEVR.asi','LE1MEVR_mod.dll'){"candidate $name"|Set-Content "$root\candidate\$name"}
@('LE1MEVR.asi','LE1MEVR_mod.dll')|ForEach-Object {@{name=$_;sha256=(Get-FileHash "$root\candidate\$_").Hash}}|ConvertTo-Json|Set-Content "$root\candidate\build-files.json"
foreach($present in @($false,$true)){
 foreach($fail in @($false,$true)){
  'original host'|Set-Content "$asi\LE1MEVR.asi"
  if($present){'original module'|Set-Content "$asi\LE1MEVR_mod.dll"}
  $hostHash=(Get-FileHash "$asi\LE1MEVR.asi").Hash
  $moduleHash=if($present){(Get-FileHash "$asi\LE1MEVR_mod.dll").Hash}else{$null}
  $global:ComparisonTestFail=$fail;$caught=$false
  $out="$root\case-$present-$fail"
  try{& "$root\source\tools\qa-hubs\test-startup-build.ps1" -CandidateDirectory "$root\candidate" -OutputDirectory $out -PackageDirectory fixture -LocalAppData "$root\local"}
  catch{if($_.Exception.Message -ne 'Expected mock native failure'){throw};$caught=$true}
  if($caught -ne $fail){throw 'Unexpected batch outcome'}
  if((Get-FileHash "$asi\LE1MEVR.asi").Hash -ne $hostHash -or (Test-Path "$asi\LE1MEVR_mod.dll") -ne $present){throw 'Original binary presence/hash changed'}
  if($present -and (Get-FileHash "$asi\LE1MEVR_mod.dll").Hash -ne $moduleHash){throw 'Original module hash changed'}
  if(-not (Get-Content "$out\restored-build.json" -Raw|ConvertFrom-Json).restored -or (Test-Path "$root\local\MEVR\qa-build.lock")){throw 'Restoration receipt or lease incorrect'}
 }
}
"Binary restoration passed for present/absent modules and successful/failed batches: $root"
