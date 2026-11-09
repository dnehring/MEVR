# SPDX-License-Identifier: MIT
# Disable optional shading effects for a manual stereo comparison. No game launch or binary changes.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$GameRoot,
 [Parameter(Mandatory)][string]$ReceiptDirectory,
 [ValidateSet('Disable','Restore')][string]$Action='Disable',
 [ValidateSet('Both','DynamicShadows','AmbientOcclusion')][string]$Effect='Both')
$ErrorActionPreference='Stop'
if(Get-Process MassEffect1 -ErrorAction SilentlyContinue){throw 'Close ME1 before changing the saved shading settings.'}
$game=[IO.Path]::GetFullPath((Join-Path $GameRoot 'Game/ME1'))
$files=@("$game/Binaries/Win64/ASI/LE1MEVR.ini","$game/BioGame/Config/BIOEngine.ini","$game/BioGame/Config/GamerSettings.ini")
$receipt=[IO.Path]::GetFullPath($ReceiptDirectory)
if($Action -eq 'Restore'){
    $rows=@(Get-Content -LiteralPath "$receipt/changed.json" -Raw|ConvertFrom-Json)
    if($rows.Count -ne $files.Count){throw 'Invalid shading receipt.'}
    for($i=0;$i -lt $files.Count;$i++){
        $saved=Join-Path $receipt "$i.ini"
        if([IO.Path]::GetFullPath($rows[$i].path) -ne [IO.Path]::GetFullPath($files[$i]) -or
           (Get-FileHash -LiteralPath $files[$i]).Hash -ne $rows[$i].after -or
           (Get-FileHash -LiteralPath $saved).Hash -ne $rows[$i].before){
            throw 'Settings changed since the shading test; refusing to overwrite newer player settings.'
        }
    }
    for($i=0;$i -lt $files.Count;$i++){Copy-Item -LiteralPath "$receipt/$i.ini" -Destination $files[$i] -Force}
    Write-Output 'Original shading-test settings restored.'
    return
}
if(Test-Path -LiteralPath $receipt){throw 'Use a new receipt directory.'}
foreach($file in $files){if(-not (Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing settings: $file"}}
New-Item -ItemType Directory -Path $receipt|Out-Null
$rows=@(for($i=0;$i -lt $files.Count;$i++){
    Copy-Item -LiteralPath $files[$i] -Destination "$receipt/$i.ini"
    @{path=$files[$i];before=(Get-FileHash -LiteralPath $files[$i]).Hash}
})
$rows|ConvertTo-Json -Depth 3|Set-Content -LiteralPath "$receipt/before.json"
if(-not ('MevrStereoShadingIni' -as [type])){
    Add-Type @'
using System.Runtime.InteropServices;
using System.Text;
public static class MevrStereoShadingIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
 public static extern uint GetPrivateProfileString(string section, string key, string fallback, StringBuilder value, uint size, string path);
}
'@
}
$keys=if($Effect -eq 'Both'){@('DynamicShadows','AmbientOcclusion')}else{@($Effect)}
for($i=0;$i -lt $files.Count;$i++){
    foreach($key in $keys){
        if(-not [MevrStereoShadingIni]::WritePrivateProfileString('SystemSettings',$key,'False',$files[$i])){throw "INI write failed: $key"}
        $value=[Text.StringBuilder]::new(64)
        [void][MevrStereoShadingIni]::GetPrivateProfileString('SystemSettings',$key,'missing',$value,64,$files[$i])
        if($value.ToString() -ne 'False'){throw "INI readback failed: $key"}
    }
    $rows[$i].after=(Get-FileHash -LiteralPath $files[$i]).Hash
}
$rows|ConvertTo-Json -Depth 3|Set-Content -LiteralPath "$receipt/changed.json"
Write-Output "$($keys -join ', ') disabled for the next launch. Backup: $receipt"
