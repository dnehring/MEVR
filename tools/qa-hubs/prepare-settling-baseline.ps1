# SPDX-License-Identifier: MIT
# Freeze current combined source, retaining candidate settings files before substituting the reviewed baseline.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$StageDirectory,[Parameter(Mandatory)][string]$BaselineSettings,
 [Parameter(Mandatory)][string]$BaselineTests)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage=[IO.Path]::GetFullPath($StageDirectory)
if(-not $stage.StartsWith(([IO.Path]::GetFullPath("$repo\scratch")+'\'),[StringComparison]::OrdinalIgnoreCase) -or (Test-Path $stage)){throw 'Use a new directory under checkout/scratch.'}
if(Get-Process MassEffect1,MSBuild,cl,link -ErrorAction SilentlyContinue){throw 'Finish native sessions and builds first.'}
New-Item -ItemType Directory -Path "$stage\source","$stage\candidate-inputs"|Out-Null
Start-Transcript -LiteralPath "$stage\prepare.txt"|Out-Null
try{
 Copy-Item -LiteralPath "$repo\CMakeLists.txt" -Destination "$stage\source"
 foreach($name in 'src','assets','tools','tests','docs'){Copy-Item -LiteralPath "$repo\$name" -Destination "$stage\source" -Recurse}
 foreach($name in 'external','third_party','reference'){New-Item -ItemType Junction -Path "$stage\source\$name" -Target "$repo\$name"|Out-Null}
 Copy-Item -LiteralPath "$stage\source\src\game\SystemSettings.cpp" -Destination "$stage\candidate-inputs\SystemSettings.cpp"
 Copy-Item -LiteralPath "$stage\source\tests\SystemSettingsTests.cpp" -Destination "$stage\candidate-inputs\SystemSettingsTests.cpp"
 Copy-Item -LiteralPath $BaselineSettings -Destination "$stage\source\src\game\SystemSettings.cpp" -Force
 Copy-Item -LiteralPath $BaselineTests -Destination "$stage\source\tests\SystemSettingsTests.cpp" -Force
 cmake -S "$stage\source" -B "$stage\build" -G 'Visual Studio 17 2022' -A x64 -DMEVR_HOT_RELOAD=ON -DMEVR_LE1_LESDK=ON -DMEVR_VOICE=ON -DMEVR_BUILD_TESTS=ON -DMEVR_VR_LAUNCHER=OFF
 if($LASTEXITCODE){throw 'Frozen source configuration failed.'}
 & "$repo\tools\qa-hubs\build-startup-candidate.ps1" -SourceDirectory "$stage\source" -BuildDirectory "$stage\build" -OutputDirectory "$stage\baseline"
}finally{Stop-Transcript|Out-Null}
