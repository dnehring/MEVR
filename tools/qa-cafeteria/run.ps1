# SPDX-License-Identifier: MIT
# Requires PowerShell 7. See README.md before taking the shared game/SteamVR session.
# Every run writes a new private folder, then restores its own backup in finally.
#Requires -Version 7.0
param(
    [ValidateSet('High','Current')][string]$Preset='High', [switch]$SkipCapture, [switch]$Plan, [switch]$Diagnostic,
    [string]$ExpectedAsiSha256='', [ValidateSet('Active','LegacyQaMarker')][string]$RuntimeMode='Active',
    [string]$SteamRoot='', [string]$SteamVRRoot='', [string]$GameRoot='', [string]$ProfileRoot='',
    [string]$LocalAppData=[Environment]::GetFolderPath('LocalApplicationData'),
    [string]$PythonExe='python', [string]$OutputRoot=''
)
. "$PSScriptRoot\common.ps1"
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $SteamRoot){$SteamRoot=(Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/','\'}
if(-not $GameRoot){$GameRoot=Find-QaInstall $SteamRoot '1328670'}
if(-not $SteamVRRoot){$SteamVRRoot=Find-QaInstall $SteamRoot '250820'}
if(-not $ProfileRoot){$ProfileRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'}
if(-not $OutputRoot){$OutputRoot=Join-Path $repo 'scratch\qa-cafeteria'}
$PythonExe=(Get-Command $PythonExe -CommandType Application -ErrorAction Stop).Source
$asi=Join-Path $GameRoot 'Game\ME1\Binaries\Win64\ASI\LE1MEVR.asi'
foreach($path in @($asi,$ProfileRoot,(Join-Path $SteamRoot 'steam.exe'),(Join-Path $SteamVRRoot 'bin\win64\vrstartup.exe'))){
    if(-not (Test-Path -LiteralPath $path)){throw "Required path missing: $path"}
}
$actual=(Get-FileHash -LiteralPath $asi).Hash
$runtime=Get-QaRuntime
$ctx=[ordered]@{RepoRoot=$repo;Preset=$Preset;SkipCapture=[bool]$SkipCapture;Diagnostic=[bool]$Diagnostic;SteamRoot=[IO.Path]::GetFullPath($SteamRoot);
    SteamVRRoot=[IO.Path]::GetFullPath($SteamVRRoot);GameRoot=[IO.Path]::GetFullPath($GameRoot);ProfileRoot=[IO.Path]::GetFullPath($ProfileRoot);
    LocalAppData=[IO.Path]::GetFullPath($LocalAppData);PythonExe=$PythonExe;ExpectedAsiSha256=$ExpectedAsiSha256;
    InstalledAsiSha256=$actual;RuntimeMode=$RuntimeMode;ActiveRuntime=$runtime;PowerShell=$PSVersionTable.PSVersion.ToString();
    OutputRoot=[IO.Path]::GetFullPath($OutputRoot);StartingMap='BIOA_NOR00';StartingPosition=@(-5170.5,14705.2,-36047.9);StartingYaw=-16164}
if($Plan){$ctx|ConvertTo-Json -Depth 5;return}
if($ExpectedAsiSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or $actual -ne $ExpectedAsiSha256){throw 'Supply the reviewed installed ASI hash with -ExpectedAsiSha256. Use -Plan to inspect it.'}
if($RuntimeMode -eq 'LegacyQaMarker'){
    # This marker exists ONLY in the private binary used for the original route validation.
    if($actual -ne 'CE247578A6672A7786B024010EDEC1B11725791EEB8FF65F7A96D939EC89F216'){throw 'Legacy runtime marker is not supported by this binary. Use Active with SteamVR selected.'}
}elseif([IO.Path]::GetFullPath($runtime) -ne [IO.Path]::GetFullPath((Join-Path $SteamVRRoot 'steamxr_win64.json'))){
    throw 'SteamVR must already be the active OpenXR runtime. This runner does not change the global runtime.'
}
if(@(Get-QaProcesses).Count){throw 'Another game, launcher or SteamVR session is active. Coordinate ownership first.'}
$data=Join-Path $LocalAppData 'MEVR'
if(@(Get-ChildItem -LiteralPath $data -Filter '*.request' -ErrorAction SilentlyContinue).Count){throw 'Pending test-loop request exists.'}
New-Item -ItemType Directory -Path $data -Force|Out-Null
$lockPath=Join-Path $data 'qa-cafeteria.lock'
# CreateNew preserves stale locks after an interrupted run, so another agent cannot silently take over.
$lock=[IO.File]::Open($lockPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$RunDirectory=Join-Path $ctx.OutputRoot ('cafeteria-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$transcript=$false;$restored=$false
$oldLocalAppData=$env:LOCALAPPDATA
try {
    New-Item -ItemType Directory -Path $RunDirectory -Force|Out-Null
    $bytes=[Text.Encoding]::UTF8.GetBytes($RunDirectory)
    $lock.Write($bytes,0,$bytes.Length);$lock.Flush()
    $ctx|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$RunDirectory\context.json"
    $sources=Join-Path $RunDirectory 'sources'
    New-Item -ItemType Directory -Path $sources|Out-Null
    $kitCopy=Join-Path $sources 'qa-cafeteria'
    New-Item -ItemType Directory -Path $kitCopy|Out-Null
    Get-ChildItem -LiteralPath $PSScriptRoot -File|Copy-Item -Destination $kitCopy
    $dependencies=@('common.ps1','console.ps1','replay.ps1','snapshot.ps1','fake-headset.ps1','mevrrec.py','make-walk-recording.py','perf-probe.py')
    foreach($name in $dependencies){Copy-Item -LiteralPath (Join-Path $repo "tools\$name") -Destination $sources}
    @(Get-ChildItem -LiteralPath $sources -Recurse -File|ForEach-Object {@{name=[IO.Path]::GetRelativePath($sources,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$RunDirectory\source-hashes.json"
    Start-Transcript -LiteralPath "$RunDirectory\transcript.txt"|Out-Null;$transcript=$true
    $env:LOCALAPPDATA=$LocalAppData
    & "$PSScriptRoot\session.ps1" -Action Prepare -RunDirectory $RunDirectory
    if($Preset -eq 'High'){& "$PSScriptRoot\high.ps1" -RunDirectory $RunDirectory}
    & "$PSScriptRoot\launch-ready.ps1" -RunDirectory $RunDirectory
    & "$PSScriptRoot\route.ps1" -RunDirectory $RunDirectory -Tag "cafeteria-$($Preset.ToLower())"
    if($Diagnostic){& "$PSScriptRoot\diagnose.ps1" -RunDirectory $RunDirectory}
    elseif(-not $SkipCapture){& "$PSScriptRoot\capture.ps1" -RunDirectory $RunDirectory -Tag "cafeteria-$($Preset.ToLower())-turns"}
}finally{
    try {
        if(Test-Path -LiteralPath "$RunDirectory\before.json"){
            if(Get-Process MassEffect1 -ErrorAction SilentlyContinue){
                [void](Assert-QaOwnedGame)
                Invoke-QaTool 'replay.ps1' @{Stop=$true}
                Invoke-QaTool 'console.ps1' @{Command=@('exit')}
            }
            $until=(Get-Date).AddSeconds(60)
            while((Get-Process MassEffect1,MassEffectLauncher -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until){Start-Sleep -Milliseconds 500}
            & "$PSScriptRoot\session.ps1" -Action Restore -RunDirectory $RunDirectory
        }
        $restored=$true
    }finally{
        if($transcript){Stop-Transcript|Out-Null}
        $lock.Dispose()
        if($restored){Remove-Item -LiteralPath $lockPath}
        else{Write-Warning "Recovery required. Lock retained: $lockPath. See README.md and $RunDirectory"}
        $env:LOCALAPPDATA=$oldLocalAppData
        Write-Host "Run artifacts: $RunDirectory"
    }
}
