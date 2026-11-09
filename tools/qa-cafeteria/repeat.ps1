# SPDX-License-Identifier: MIT
# Three independently restored native runs by default. Stop on the first failure.
#Requires -Version 7.0
param([ValidateRange(1,5)][int]$Count=3,[Parameter(Mandatory)][string]$ExpectedAsiSha256,
      [ValidateSet('High','Current')][string]$Preset='High',
      [ValidateSet('Active','LegacyQaMarker')][string]$RuntimeMode='Active',
      [string]$PythonExe='python',[string]$OutputRoot='',[string]$GameRoot='')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $OutputRoot){$OutputRoot=Join-Path $repo ('scratch\qa-cafeteria\batch-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))}
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
@{created=(Get-Date).ToString('o');count=$Count;preset=$Preset;runtimeMode=$RuntimeMode;expectedAsiSha256=$ExpectedAsiSha256;python=$PythonExe}|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputRoot 'batch.json')
for($index=1;$index -le $Count;$index++){
    Write-Host "Starting validation run $index of $Count"
    & "$PSScriptRoot\run.ps1" -Preset $Preset -ExpectedAsiSha256 $ExpectedAsiSha256 -RuntimeMode $RuntimeMode -PythonExe $PythonExe -OutputRoot $OutputRoot -GameRoot $GameRoot
    if($LASTEXITCODE){throw "Run $index returned a failure code."}
}
"Completed $Count runs. Artifacts: $OutputRoot"
