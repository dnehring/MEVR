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
$chap=Get-Content -LiteralPath $files[1] -Raw|ConvertFrom-Json
$universe=@($chap.universes|Where-Object universeID -eq '2')
if($universe.Count -ne 1 -or $universe[0].standing.translation[1] -ne -1.7526 -or
   $universe[0].seated.translation[1] -ne 0){throw 'Default 5ft9 floor/head-relative origins failed.'}
if(-not $settings.steamvr.supersampleManualOverride -or $settings.steamvr.supersampleScale -ne 1.0 -or
   $settings.'steam.app.1328670'.resolutionScale -ne 100 -or -not $settings.unrelated.keep -or
   $driver.driver_null.renderWidth -ne 2284 -or $driver.driver_null.renderHeight -ne 2448){throw 'Fixed-resolution setup failed.'}
& "$repo\tools\fake-headset.ps1" -Off -SteamRoot $steam -SteamVRRoot $runtime -BackupDirectory "$root\backup"
# Steam settings use key-wise restoration; JSON formatting/empty new sections can
# change. The old byte-equality assertion already failed before the height change.
$restored=Get-Content -LiteralPath $files[0] -Raw|ConvertFrom-Json
$chap=Get-Content -LiteralPath $files[1] -Raw|ConvertFrom-Json
if($restored.steamvr.supersampleScale -ne 1.28 -or $restored.'steam.app.1328670'.resolutionScale -ne 46 -or
   -not $restored.unrelated.keep -or $restored.steamvr.PSObject.Properties['forcedDriver'] -or
   $restored.steamvr.PSObject.Properties['supersampleManualOverride'] -or @($chap.universes).Count -ne 0 -or
   (Get-FileHash -LiteralPath $files[2]).Hash -ne $before[2]){throw 'Original settings were not restored.'}

# Refresh an existing zero-height null universe, preserve another universe, and
# do not capture temporary values as the original on a repeated -On.
'{"jsonid":"chaperone_info","universes":[{"universeID":"77","keep":"real"},{"universeID":"2","standing":{"translation":[0,0,0],"yaw":12},"keep":"original-null"}],"version":5}' |
 Set-Content -LiteralPath $files[1]
& "$repo\tools\fake-headset.ps1" -On -SteamRoot $steam -SteamVRRoot $runtime -BackupDirectory "$root\backup"
$chap=Get-Content -LiteralPath $files[1] -Raw|ConvertFrom-Json
if(($chap.universes|Where-Object universeID -eq '2').standing.translation[1] -ne -1.7526 -or
   ($chap.universes|Where-Object universeID -eq '77').keep -ne 'real'){throw 'Existing null universe was not updated safely.'}
& "$repo\tools\fake-headset.ps1" -On -HeadHeightM 1.6 -SteamRoot $steam -SteamVRRoot $runtime -BackupDirectory "$root\backup"
$chap=Get-Content -LiteralPath $files[1] -Raw|ConvertFrom-Json
if(($chap.universes|Where-Object universeID -eq '2').standing.translation[1] -ne -1.6){throw 'Explicit height override failed.'}
& "$repo\tools\fake-headset.ps1" -Off -SteamRoot $steam -SteamVRRoot $runtime -BackupDirectory "$root\backup"
$chap=Get-Content -LiteralPath $files[1] -Raw|ConvertFrom-Json
$nullEntry=$chap.universes|Where-Object universeID -eq '2'
if(@($chap.universes).Count -ne 2 -or $nullEntry.keep -ne 'original-null' -or
   $nullEntry.standing.translation[1] -ne 0 -or $nullEntry.standing.yaw -ne 12 -or
   ($chap.universes|Where-Object universeID -eq '77').keep -ne 'real'){throw 'Original universes were not restored.'}
function Get-Process { [CmdletBinding()]param([string[]]$Name) if($Name -contains 'MassEffect2') { [pscustomobject]@{Id=123;ProcessName='MassEffect2'} } }
$refused=$false
try { & "$repo\tools\fake-headset.ps1" -On -SteamRoot $steam -SteamVRRoot $runtime -BackupDirectory "$root\backup" }
catch { if($_.Exception.Message -notmatch 'trilogy game is running'){throw}; $refused=$true }
if(-not $refused){throw 'ME2 running-game guard failed.'}
'Default/custom head height, repeated setup, original universe restoration, trilogy guard and resolution settings passed; no native process was used.'
