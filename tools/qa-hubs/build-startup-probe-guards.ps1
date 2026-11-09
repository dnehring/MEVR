# SPDX-License-Identifier: MIT
# Apply the two reviewed loading read guards to a frozen baseline; retain old symbols/source for diagnosis.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$StageDirectory,[string]$PythonExe='python')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage=[IO.Path]::GetFullPath($StageDirectory)
$baseline=Get-Content "$stage\baseline\source-hashes.json" -Raw|ConvertFrom-Json
foreach($entry in $baseline){if((Get-FileHash "$stage\source\$($entry.path)").Hash -ne $entry.sha256){throw 'Frozen baseline source changed; inspect before applying common read guards.'}}
New-Item -ItemType Directory -Path "$stage\fault-diagnosis"|Out-Null
Copy-Item "$stage\build\Release\LE1MEVR.pdb" "$stage\baseline\LE1MEVR.pdb"
& $PythonExe "$repo\tools\resolve-native-symbols.py" "$stage\baseline\LE1MEVR.asi" 0x4BCAD6 0x3ADA8D |
 Set-Content "$stage\fault-diagnosis\symbols.json"
if($LASTEXITCODE){throw 'Original fault symbol resolution failed.'}
$receipt=@(foreach($name in 'head_aim.cpp','pchud.cpp'){
 $relative="src\halcyon\le1\$name"
 Copy-Item "$stage\source\$relative" "$stage\fault-diagnosis\$name"
 $old=(Get-FileHash "$stage\source\$relative").Hash
 Copy-Item "$repo\$relative" "$stage\source\$relative" -Force
 @{path=$relative;old=$old;new=(Get-FileHash "$stage\source\$relative").Hash}
})
$receipt|ConvertTo-Json|Set-Content "$stage\fault-diagnosis\source-changes.json"
& "$PSScriptRoot\build-startup-candidate.ps1" -SourceDirectory "$stage\source" -BuildDirectory "$stage\build" -OutputDirectory "$stage\baseline-probe-safe"
