# SPDX-License-Identifier: MIT
# Exercise real INI edits/restoration against private synthetic files, never the installed game.
#Requires -Version 7.0
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$repo\tools\qa-cafeteria\state.ps1"
$root=Join-Path $repo ('scratch\qa-hubs\workloads-offline-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$game=Join-Path $root 'game\Game\ME1';$asi=Join-Path $game 'Binaries\Win64\ASI'
New-Item -ItemType Directory -Path $asi,"$game\BioGame\Config"|Out-Null
@'
[Performance]
sharePass=5
minScreenRadiusTenths=130
[SystemSettings]
AntiAliasing=True
DynamicShadows=True
[Unrelated]
preserve=yes
'@|Set-Content -LiteralPath "$asi\LE1MEVR.ini"
"[global]`nprofile=fixture`n[profile:fixture]`nsfr2ResX=3840`nsfr2ResY=3840`nsfr2=1"|Set-Content -LiteralPath "$asi\MELEVR.ini"
"[SystemSettings]`nResX=3840`nResY=3840`nAntiAliasing=True"|Set-Content -LiteralPath "$game\BioGame\Config\GamerSettings.ini"
"[SystemSettings]`nAntiAliasing=True"|Set-Content -LiteralPath "$game\BioGame\Config\BIOEngine.ini"
'fixture host'|Set-Content -LiteralPath "$asi\LE1MEVR.asi"
foreach($name in @('Quality','Balanced','Performance')){
 $run=Join-Path $root $name;New-Item -ItemType Directory -Path $run|Out-Null
 @{GameRoot="$root\game";EyeWidth=2284;EyeHeight=2448}|ConvertTo-Json|Set-Content -LiteralPath "$run\context.json"
 $files=@("$asi\LE1MEVR.ini","$asi\MELEVR.ini","$game\BioGame\Config\GamerSettings.ini","$game\BioGame\Config\BIOEngine.ini")
 Save-QaBackup $run $files @() (Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash 'fixture'
 & "$PSScriptRoot\set-workload.ps1" -RunDirectory $run -Workload $name
 $settings=Get-Content -LiteralPath "$asi\LE1MEVR.ini" -Raw
 if($settings -notmatch 'sharePass=5' -or $settings -notmatch 'AntiAliasing=True' -or $settings -notmatch 'preserve=yes'){
  throw 'Workload modified an unrelated choice'
 }
 $observed=Get-Content -LiteralPath "$run\workload.json" -Raw|ConvertFrom-Json
 $expected=switch($name){'Quality'{@(2284,2448)}'Balanced'{@(1942,2080)}'Performance'{@(1714,1836)}}
 if(($observed.source -join ',') -ne ($expected -join ',')){throw 'Unexpected rounded menu resolution'}
 Restore-QaBackup $run
 if(@(Test-QaBackup (Get-Content -LiteralPath "$run\before.json" -Raw|ConvertFrom-Json)).Count){throw 'Restoration failed'}
}
$fixed=$null
foreach($radius in @(8,4,0)){
 $run=Join-Path $root "cull-$radius";New-Item -ItemType Directory -Path $run|Out-Null
 @{GameRoot="$root\game";EyeWidth=2284;EyeHeight=2448}|ConvertTo-Json|Set-Content -LiteralPath "$run\context.json"
 Save-QaBackup $run $files @() (Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash 'fixture'
 & "$PSScriptRoot\set-workload.ps1" -RunDirectory $run -Workload Quality -CullRadiusPx $radius
 $observed=Get-Content -LiteralPath "$run\workload.json" -Raw|ConvertFrom-Json
 if($observed.cullPx -ne $radius -or ($observed.source -join ',') -ne '2284,2448'){throw 'Culling override changed resolution or failed to apply'}
 $normalized=@($files|ForEach-Object {(Get-Content -LiteralPath $_ -Raw) -replace 'minScreenRadiusPx=\d+','minScreenRadiusPx=TEST'}) -join "`n"
 if($null -ne $fixed -and $normalized -ne $fixed){throw 'Culling override changed another setting'}
 $fixed=$normalized
 Restore-QaBackup $run
 if(@(Test-QaBackup (Get-Content -LiteralPath "$run\before.json" -Raw|ConvertFrom-Json)).Count){throw 'Culling restoration failed'}
}
$manifest=Get-Content -LiteralPath "$run\before.json" -Raw|ConvertFrom-Json
'broken'|Set-Content -LiteralPath $manifest.files[0].saved
$refused=$false
try{& "$PSScriptRoot\set-workload.ps1" -RunDirectory $run -Workload Quality}catch{$refused=$true}
if(-not $refused -or @(Test-QaBackup $manifest).Count){throw 'Damaged backup was not rejected before editing'}
Write-Host "Three preset edits, three isolated culling edits, exact restoration and damaged-backup refusal passed: $root"
