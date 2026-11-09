# SPDX-License-Identifier: MIT
# Exercise the actual null-headset JSON edits/restoration with native calls replaced by no-ops.
#Requires -Version 7.0
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root=Join-Path $repo ('scratch\qa-hubs\fake-resolution-test-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$steam=Join-Path $root 'Steam';$runtime=Join-Path $root 'SteamVR'
New-Item -ItemType Directory -Path "$steam\config","$runtime\drivers\null\resources\settings"|Out-Null
'{"steamvr":{"supersampleScale":1.28},"steam.app.1328670":{"resolutionScale":46},"unrelated":{"keep":true}}'|
 Set-Content -LiteralPath "$steam\config\steamvr.vrsettings"
'{"driver_null":{"enable":false,"renderWidth":1512,"renderHeight":1680}}'|
 Set-Content -LiteralPath "$runtime\drivers\null\resources\settings\default.vrsettings"
'{"jsonid":"chaperone_info","universes":[],"version":5}'|Set-Content -LiteralPath "$steam\config\chaperone_info.vrchap"
$files=@("$steam\config\steamvr.vrsettings","$steam\config\chaperone_info.vrchap","$runtime\drivers\null\resources\settings\default.vrsettings")
$before=@($files|ForEach-Object {(Get-FileHash -LiteralPath $_).Hash})
function Get-Process { [CmdletBinding()]param([string[]]$Name) }
function Stop-Process { [CmdletBinding()]param([switch]$Force) process{} }
function Start-Sleep { param([int]$Seconds) }
function Start-Process { param([string]$FilePath,[string]$WindowStyle) }
& "$repo\tools\fake-headset.ps1" -On -Width 2284 -Height 2448 -SteamRoot $steam -SteamVRRoot $runtime `
 -BackupDirectory "$root\backup" -AppResolutionScale 100 -FixedRenderScale
$settings=Get-Content -LiteralPath $files[0] -Raw|ConvertFrom-Json
$driver=Get-Content -LiteralPath $files[2] -Raw|ConvertFrom-Json
if(-not $settings.steamvr.supersampleManualOverride -or $settings.steamvr.supersampleScale -ne 1.0 -or
   $settings.'steam.app.1328670'.resolutionScale -ne 100 -or -not $settings.unrelated.keep -or
   $driver.driver_null.renderWidth -ne 2284 -or $driver.driver_null.renderHeight -ne 2448){throw 'Fixed-resolution setup failed.'}
& "$repo\tools\fake-headset.ps1" -Off -SteamRoot $steam -SteamVRRoot $runtime -BackupDirectory "$root\backup"
for($i=0;$i -lt $files.Count;$i++){if((Get-FileHash -LiteralPath $files[$i]).Hash -ne $before[$i]){throw 'Original settings were not restored byte-for-byte.'}}
'Fixed global/app scale and byte-identical restoration passed; no native process was used.'
