# SPDX-License-Identifier: MIT
# Temporarily install a reviewed private candidate, run the full workload, then restore the installed binary.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$CandidateReceipt,
      [Parameter(Mandatory)][string]$ExpectedInstalledSha256,
      [Parameter(Mandatory)][string]$GameRoot,
      [string]$PythonExe='python',[ValidateRange(1,5)][int]$Count=3)
$ErrorActionPreference='Stop'
$candidate=Get-Content -LiteralPath $CandidateReceipt -Raw|ConvertFrom-Json
if(-not $candidate.privateQaOnly -or (Get-FileHash -LiteralPath $candidate.binary).Hash -ne $candidate.sha256){throw 'Candidate receipt/hash mismatch.'}
if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher,vrserver,vrcompositor -ErrorAction SilentlyContinue){throw 'Coordinate and close existing native sessions first.'}
$installed=Join-Path $GameRoot 'Game\ME1\Binaries\Win64\ASI\LE1MEVR.asi'
if((Get-FileHash -LiteralPath $installed).Hash -ne $ExpectedInstalledSha256){throw 'Installed binary changed since review.'}
$directory=Join-Path (Split-Path $CandidateReceipt -Parent) ('validation-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $directory|Out-Null
$backup=Join-Path $directory 'before.asi'
Copy-Item -LiteralPath $installed -Destination $backup
if((Get-FileHash -LiteralPath $backup).Hash -ne $ExpectedInstalledSha256){throw 'Binary backup mismatch.'}
@{installed=$installed;backup=$backup;originalSha256=$ExpectedInstalledSha256;candidateSha256=$candidate.sha256;candidateReceipt=$CandidateReceipt}|
    ConvertTo-Json|Set-Content -LiteralPath "$directory\installation.json"
Start-Transcript -LiteralPath "$directory\transcript.txt"|Out-Null
try{
    Copy-Item -LiteralPath $candidate.binary -Destination $installed -Force
    if((Get-FileHash -LiteralPath $installed).Hash -ne $candidate.sha256){throw 'Candidate installation hash mismatch.'}
    & "$($candidate.source)\tools\qa-cafeteria\repeat.ps1" -Count $Count -Preset High -RuntimeMode LegacyQaMarker -ExpectedAsiSha256 $candidate.sha256 -PythonExe $PythonExe -OutputRoot "$directory\batch" -GameRoot $GameRoot
    & "$PSScriptRoot\report.ps1" -BatchDirectory "$directory\batch" -PythonExe $PythonExe
    & $PythonExe "$PSScriptRoot\check-camera-diagnostic.py" "$directory\batch"
    if($LASTEXITCODE){throw 'Camera diagnostic validation failed.'}
}finally{
    try{
        if(Get-Process MassEffect1,MassEffectLauncher -ErrorAction SilentlyContinue){throw "Game still running. Keep backup and recover inner run before restoring $installed"}
        $actual=(Get-FileHash -LiteralPath $installed).Hash
        if($actual -ne $candidate.sha256 -and $actual -ne $ExpectedInstalledSha256){throw 'Another task changed the binary; refusing restoration.'}
        Copy-Item -LiteralPath $backup -Destination $installed -Force
        if((Get-FileHash -LiteralPath $installed).Hash -ne $ExpectedInstalledSha256){throw 'Original binary restoration failed.'}
        @{restored=(Get-Date).ToString('o');sha256=$ExpectedInstalledSha256}|ConvertTo-Json|Set-Content -LiteralPath "$directory\binary-restored.json"
        & "$PSScriptRoot\verify-camera-restoration.ps1" -ValidationDirectory $directory
        "Original binary restored. Validation artifacts: $directory"
    }finally{Stop-Transcript|Out-Null}
}
