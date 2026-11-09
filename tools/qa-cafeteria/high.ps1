# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory)
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
$ErrorActionPreference='Stop'
if(Get-Process MassEffect1,MassEffectLauncher -ErrorAction SilentlyContinue){throw 'Close game first.'}
$game=Join-Path $ctx.GameRoot 'Game\ME1'
$asi=Join-Path $game 'Binaries\Win64\ASI'
$backup=Get-Content -LiteralPath "$RunDirectory\before.json" -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath "$asi\LE1MEVR.asi").Hash -ne $backup.binary){throw 'Binary changed.'}
Add-Type @'
using System.Runtime.InteropServices;
public static class WalkHighIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
}
'@
function SetKey($path,$section,$key,$value){if(-not [WalkHighIni]::WritePrivateProfileString($section,$key,$value,$path)){throw "INI write failed: $key"}}
$ini=Join-Path $asi 'MELEVR.ini'
$active=(Select-String -LiteralPath $ini -Pattern '^profile\s*=\s*(.+)$' | Select-Object -First 1).Matches[0].Groups[1].Value.Trim()
foreach($key in @('sfr2ResX','sfr2ResY','expectedResX','expectedResY')){SetKey $ini "profile:$active" $key '3840'}
foreach($key in @('ResX','ResY')){SetKey "$game\BioGame\Config\GamerSettings.ini" 'SystemSettings' $key '3840'}
foreach($file in @("$asi\LE1MEVR.ini","$game\BioGame\Config\GamerSettings.ini","$game\BioGame\Config\BIOEngine.ini")){
 foreach($pair in @(@('DynamicShadows','True'),@('AmbientOcclusion','True'),@('FoliageDrawRadiusMultiplier','1.0'),@('AntiAliasing','True'),@('MotionBlur','False'))){SetKey $file 'SystemSettings' $pair[0] $pair[1]}
}
'High workload: 3840x3840 source, dynamic shadows and AO on, foliage distance 1.0, AA on, motion blur off. One Draw unchanged.'
