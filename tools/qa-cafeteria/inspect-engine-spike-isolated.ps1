# SPDX-License-Identifier: MIT
# Inspect a byte-verified private copy when the existing Ghidra project is open elsewhere.
#Requires -Version 7.0
param([string[]]$Target=@('0x6F2AC','0x1728DE','0x414E18','0x41C189'))
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$env:LOCALAPPDATA=[Environment]::GetFolderPath('LocalApplicationData')
. "$repo\tools\ghidra\common.ps1"
$original=Get-GhidraGame LE1
if(-not (Test-Path -LiteralPath $original.Marker)){throw 'Existing imported LE1 research project is required.'}
$sourceRoot=$GhidraRoot
$copy=Join-Path $sourceRoot ('isolated\engine-spike-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $copy|Out-Null
$all=@(Get-Item -LiteralPath $original.Gpr)+@(Get-ChildItem -LiteralPath $original.Rep -Recurse -File -Force)
# Ghidra holds its unsaved temporary page store exclusively. Copy the stable committed database files only;
# this deliberately excludes unsaved analysis in the open project, not any saved database version.
$excluded=@($all|Where-Object {$_.Name -match '^tmp\d+\.ps$'})
$files=@($all|Where-Object {$_.Name -notmatch '^tmp\d+\.ps$'})
$hashes=@($files|ForEach-Object {@{relative=[IO.Path]::GetRelativePath($sourceRoot,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
foreach($entry in $hashes){
 $destination=Join-Path $copy $entry.relative
 New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force|Out-Null
 Copy-Item -LiteralPath (Join-Path $sourceRoot $entry.relative) -Destination $destination
}
foreach($entry in $hashes){
 if((Get-FileHash -LiteralPath (Join-Path $sourceRoot $entry.relative)).Hash -ne $entry.sha256 -or
    (Get-FileHash -LiteralPath (Join-Path $copy $entry.relative)).Hash -ne $entry.sha256){throw 'Project changed during copying; private copy is not admitted.'}
}
$hashes|ConvertTo-Json -Depth 3|Set-Content -LiteralPath "$copy\source-hashes.json"
# All locks, cache, output and settings now belong to the copy. The original project is never closed or edited.
$GhidraRoot=$copy;$GhidraOut=Join-Path $copy 'out';$GhidraLogs=Join-Path $copy 'logs'
Assert-Ghidra
$cache=Join-Path $GhidraOut 'LE1';New-Item -ItemType Directory -Path $cache|Out-Null
$out=Join-Path $cache 'last.txt';$argsFile=Join-Path $cache 'args.txt'
[IO.File]::WriteAllLines($argsFile,[string[]](@($out,$cache,'decompile')+$Target),[Text.UTF8Encoding]::new($false))
$arguments=@($copy,'LE1','-process','MassEffect1.exe','-noanalysis','-readOnly','-scriptPath',$GhidraScripts,'-postScript','MevrDecompile.java',"@$argsFile")
$code=Invoke-Headless $arguments (Join-Path $GhidraLogs 'engine-spike.log')
if($code -or -not (Test-Path -LiteralPath $out)){throw "Isolated decompile failed ($code); retained at $copy"}
@{sourceProject=$sourceRoot;isolatedProject=$copy;targets=$Target;output=$out;readOnly=$true;
 excludedUnsavedPageStores=@($excluded.Name);scope='Saved database snapshot; unsaved open-project analysis excluded.'}|
 ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$copy\receipt.json"
Write-Host "Private isolated engine research: $out"
