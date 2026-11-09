# SPDX-License-Identifier: MIT
# Apply a menu-equivalent workload only inside an already backed-up measurement session.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,
 [Parameter(Mandatory)][ValidateSet('Quality','Balanced','Performance')][string]$Workload)
$ErrorActionPreference='Stop'
$ctx=Get-Content -LiteralPath "$RunDirectory\context.json" -Raw|ConvertFrom-Json
$before=Get-Content -LiteralPath "$RunDirectory\before.json" -Raw|ConvertFrom-Json
if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Workload changes require a closed game.'}
$game=Join-Path $ctx.GameRoot 'Game\ME1';$asi=Join-Path $game 'Binaries\Win64\ASI'
if((Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash -ne $before.binary){throw 'Installed host changed.'}
$files=@("$asi\LE1MEVR.ini","$asi\MELEVR.ini","$game\BioGame\Config\GamerSettings.ini","$game\BioGame\Config\BIOEngine.ini")
foreach($file in $files){
 $saved=@($before.files|Where-Object path -eq $file)
 if($saved.Count -ne 1 -or -not $saved[0].exists -or (Get-FileHash -LiteralPath $saved[0].saved).Hash -ne $saved[0].sha256){throw "Missing/invalid protected backup: $file"}
 if((Get-FileHash -LiteralPath $file).Hash -ne $saved[0].sha256){throw "Current settings differ from backup; refusing stale session edits: $file"}
}
# Mirrors PerformanceModes.cpp and MenuSettings::HeadsetSize. AA and One Draw are intentionally retained.
$mode=switch($Workload){
 'Quality' {@{scale=1.0;px=4;foliage='0.75';ao='True'}}
 'Balanced' {@{scale=0.85;px=8;foliage='0.50';ao='True'}}
 'Performance' {@{scale=0.75;px=24;foliage='0.35';ao='False'}}
}
$width=if($mode.scale -eq 1){[int]$ctx.EyeWidth}else{2*[int][Math]::Round($ctx.EyeWidth*$mode.scale/2,[MidpointRounding]::AwayFromZero)}
$height=if($mode.scale -eq 1){[int]$ctx.EyeHeight}else{2*[int][Math]::Round($ctx.EyeHeight*$mode.scale/2,[MidpointRounding]::AwayFromZero)}
Add-Type @'
using System.Runtime.InteropServices;
public static class HubWorkloadIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
}
'@
function Set-Key($path,$section,$key,$value){
 if(-not [HubWorkloadIni]::WritePrivateProfileString($section,$key,[string]$value,$path)){throw "INI write failed: $key"}
}
$active=(Select-String -LiteralPath "$asi\MELEVR.ini" -Pattern '^profile\s*=\s*(.+)$'|Select-Object -First 1).Matches[0].Groups[1].Value.Trim()
if(-not $active){throw 'Active renderer profile missing.'}
foreach($key in @('sfr2ResX','expectedResX')){Set-Key "$asi\MELEVR.ini" "profile:$active" $key $width}
foreach($key in @('sfr2ResY','expectedResY')){Set-Key "$asi\MELEVR.ini" "profile:$active" $key $height}
Set-Key "$game\BioGame\Config\GamerSettings.ini" 'SystemSettings' 'ResX' $width
Set-Key "$game\BioGame\Config\GamerSettings.ini" 'SystemSettings' 'ResY' $height
Set-Key "$asi\LE1MEVR.ini" 'Upscale' 'method' 'fsr'
Set-Key "$asi\LE1MEVR.ini" 'Performance' 'minScreenRadiusTenths' ''
Set-Key "$asi\LE1MEVR.ini" 'Performance' 'minScreenRadiusPx' $mode.px
foreach($file in @($files[0],$files[2],$files[3])){
 Set-Key $file 'SystemSettings' 'DynamicShadows' 'False'
 Set-Key $file 'SystemSettings' 'AmbientOcclusion' $mode.ao
 Set-Key $file 'SystemSettings' 'FoliageDrawRadiusMultiplier' $mode.foliage
 Set-Key $file 'SystemSettings' 'MotionBlur' 'False'
}
@{workload=$Workload;source=@($width,$height);eye=@($ctx.EyeWidth,$ctx.EyeHeight);cullPx=$mode.px;
 foliage=$mode.foliage;dynamicShadows=$false;ambientOcclusion=($mode.ao -eq 'True');fsr=$true;aa='retained';oneDraw='retained'}|
 ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$RunDirectory\workload.json"
Write-Host "$Workload workload: ${width}x${height} source, $($mode.px) px small-object culling; AA/One Draw retained."
