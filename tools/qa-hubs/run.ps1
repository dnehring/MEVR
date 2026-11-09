# SPDX-License-Identifier: MIT
# One checked hub route using the cafeteria kit's shared backup/runtime/launch/capture mechanisms.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$ScenarioFile,[switch]$Plan,[switch]$SkipCapture,
      [ValidateSet('High','Current')][string]$Preset='High',[string]$ExpectedAsiSha256='',
      [ValidateSet('Active','LegacyQaMarker')][string]$RuntimeMode='Active',
      [string]$SteamRoot='',[string]$SteamVRRoot='',[string]$GameRoot='',[string]$ProfileRoot='',
      [string]$FixtureSaveFile='',[string]$FixtureSaveSha256='',
      [string]$LocalAppData=[Environment]::GetFolderPath('LocalApplicationData'),
      [string]$PythonExe='python',[string]$OutputRoot='')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$kit=Join-Path $repo 'tools\qa-cafeteria'
. "$kit\common.ps1"
$argsForValidator=@($ScenarioFile)
if(-not $Plan){$argsForValidator+='--for-run'}
$checked=& $PythonExe "$PSScriptRoot\scenario.py" @argsForValidator
if($LASTEXITCODE){throw ($checked-join "`n")}
$validation=($checked-join "`n")|ConvertFrom-Json
$scenario=$validation.scenario
if($Plan){$validation|ConvertTo-Json -Depth 20;return}
if(-not $SteamRoot){$SteamRoot=(Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/','\'}
if(-not $GameRoot){$GameRoot=Find-QaInstall $SteamRoot '1328670'}
if(-not $SteamVRRoot){$SteamVRRoot=Find-QaInstall $SteamRoot '250820'}
if(-not $ProfileRoot){$ProfileRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'}
if(-not $OutputRoot){$OutputRoot=Join-Path $repo 'scratch\qa-hubs'}
$PythonExe=(Get-Command $PythonExe -CommandType Application -ErrorAction Stop).Source
$asi=Join-Path $GameRoot 'Game\ME1\Binaries\Win64\ASI\LE1MEVR.asi'
foreach($path in @($asi,$ProfileRoot,(Join-Path $SteamRoot 'steam.exe'),(Join-Path $SteamVRRoot 'bin\win64\vrstartup.exe'))){
    if(-not (Test-Path -LiteralPath $path)){throw "Required path missing: $path"}
}
$actual=(Get-FileHash -LiteralPath $asi).Hash
if($ExpectedAsiSha256 -notmatch '^[A-Fa-f0-9]{64}$' -or $actual -ne $ExpectedAsiSha256){throw 'Supply the reviewed installed ASI hash.'}
$runtime=Get-QaRuntime
if($RuntimeMode -eq 'LegacyQaMarker'){
    if($actual -ne 'CE247578A6672A7786B024010EDEC1B11725791EEB8FF65F7A96D939EC89F216'){throw 'Private runtime marker is not supported by this binary.'}
}elseif([IO.Path]::GetFullPath($runtime) -ne [IO.Path]::GetFullPath((Join-Path $SteamVRRoot 'steamxr_win64.json'))){
    throw 'SteamVR must already be active; the runner does not switch the global runtime.'
}
if(@(Get-QaProcesses).Count){throw 'Coordinate exclusive ownership and close existing native sessions.'}
$fixtureArgs=@($ProfileRoot)
if($FixtureSaveFile -and $FixtureSaveSha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Supply the reviewed SHA-256 for the selected fixture.'}
if($FixtureSaveSha256 -and -not $FixtureSaveFile){throw 'A fixture hash requires its selected save file.'}
if($FixtureSaveFile){$fixtureArgs+=@('--selected',$FixtureSaveFile,'--expected-sha256',$FixtureSaveSha256)}
$fixtureJson=& $PythonExe "$PSScriptRoot\fixture.py" @fixtureArgs
if($LASTEXITCODE){throw ($fixtureJson-join "`n")}
$fixture=($fixtureJson-join "`n")|ConvertFrom-Json
$data=Join-Path $LocalAppData 'MEVR'
if(@(Get-ChildItem -LiteralPath $data -Filter '*.request' -ErrorAction SilentlyContinue).Count){throw 'Pending test-loop request exists.'}
New-Item -ItemType Directory -Path $data -Force|Out-Null
# Deliberately share the cafeteria lock: both tools own the same machine and player profile.
$lockPath=Join-Path $data 'qa-cafeteria.lock'
$lock=[IO.File]::Open($lockPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$RunDirectory=Join-Path $OutputRoot ('hub-'+$scenario.id+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$ctx=[ordered]@{RepoRoot=$repo;Preset=$Preset;SkipCapture=[bool]$SkipCapture;SteamRoot=[IO.Path]::GetFullPath($SteamRoot);
    SteamVRRoot=[IO.Path]::GetFullPath($SteamVRRoot);GameRoot=[IO.Path]::GetFullPath($GameRoot);ProfileRoot=[IO.Path]::GetFullPath($ProfileRoot);
    LocalAppData=[IO.Path]::GetFullPath($LocalAppData);PythonExe=$PythonExe;ExpectedAsiSha256=$ExpectedAsiSha256;
    InstalledAsiSha256=$actual;RuntimeMode=$RuntimeMode;ActiveRuntime=$runtime;PowerShell=$PSVersionTable.PSVersion.ToString();
    StartingMap=$scenario.expectedMap;StartingPosition=$scenario.start.positionUU;StartingYaw=$scenario.start.yawUnreal;
    ScenarioId=$scenario.id;ScenarioStatus=$scenario.status;ScenarioSha256=$validation.sha256;
    FixtureInventorySha256=$fixture.inventorySha256;FixtureSelectedSha256=$fixture.selectedSha256;
    FixtureSelectionEvidence=$fixture.selectionEvidence}
$oldLocalAppData=$env:LOCALAPPDATA;$transcript=$false;$restored=$false
try{
    New-Item -ItemType Directory -Path $RunDirectory -Force|Out-Null
    $bytes=[Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($RunDirectory));$lock.Write($bytes,0,$bytes.Length);$lock.Flush()
    # Freeze commands and analyzers so concurrent agents cannot alter a running route.
    $source=Join-Path $RunDirectory 'source\tools'
    New-Item -ItemType Directory -Path $source -Force|Out-Null
    Copy-Item -LiteralPath $kit,$PSScriptRoot -Destination $source -Recurse
    foreach($name in 'common.ps1','console.ps1','replay.ps1','snapshot.ps1','fake-headset.ps1','mevrrec.py','make-walk-recording.py','perf-probe.py'){
        Copy-Item -LiteralPath (Join-Path $repo "tools\$name") -Destination $source
    }
    $ctx.RepoRoot=[IO.Path]::GetFullPath((Split-Path $source -Parent))
    $kit=Join-Path $source 'qa-cafeteria';$hub=Join-Path $source 'qa-hubs'
    $ctx|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$RunDirectory\context.json"
    $fixture|ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$RunDirectory\starting-saves.json"
    Copy-Item -LiteralPath $ScenarioFile -Destination "$RunDirectory\scenario.json"
    if((Get-FileHash -LiteralPath "$RunDirectory\scenario.json").Hash -ne $ctx.ScenarioSha256){throw 'Scenario changed after validation; rerun its plan.'}
    @(Get-ChildItem -LiteralPath $source -File -Recurse|ForEach-Object {@{path=[IO.Path]::GetRelativePath($source,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})|
        ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$RunDirectory\source-hashes.json"
    Start-Transcript -LiteralPath "$RunDirectory\transcript.txt"|Out-Null;$transcript=$true
    $env:LOCALAPPDATA=$LocalAppData
    & "$kit\machine.ps1" -OutputDirectory $RunDirectory
    & "$kit\session.ps1" -Action Prepare -RunDirectory $RunDirectory
    $afterBackup=& $PythonExe "$hub\fixture.py" @fixtureArgs
    if($LASTEXITCODE -or (($afterBackup-join "`n")|ConvertFrom-Json).inventorySha256 -ne $ctx.FixtureInventorySha256){
        throw 'Starting saves changed during preparation; no route will run.'
    }
    if($Preset -eq 'High'){& "$kit\high.ps1" -RunDirectory $RunDirectory}
    & "$kit\launch-ready.ps1" -RunDirectory $RunDirectory -ExpectedMap $scenario.expectedMap
    & "$hub\route.ps1" -RunDirectory $RunDirectory
    if(-not $SkipCapture){
        $recording=Join-Path $RunDirectory 'hub-turn.mevrrec'
        & $PythonExe "$hub\turn.py" $recording --yaw $scenario.capture.yawDegrees --pitch $scenario.capture.pitchDegrees --sweep $scenario.capture.sweepDegrees
        if($LASTEXITCODE){throw 'Turn generation failed.'}
        & "$kit\capture.ps1" -RunDirectory $RunDirectory -Tag ($scenario.id+'-'+$Preset.ToLower()+'-turns') -Seconds $scenario.capture.seconds -RecordingPath $recording
    }
}finally{
    try{
        if(Test-Path -LiteralPath "$RunDirectory\before.json"){
            if(Get-Process MassEffect1 -ErrorAction SilentlyContinue){
                [void](Assert-QaOwnedGame)
                Invoke-QaTool 'replay.ps1' @{Stop=$true}
                Invoke-QaTool 'console.ps1' @{Command=@('exit')}
            }
            $until=(Get-Date).AddSeconds(60)
            while((Get-Process MassEffect1,MassEffectLauncher -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until){Start-Sleep -Milliseconds 500}
            & "$kit\session.ps1" -Action Restore -RunDirectory $RunDirectory
        }
        $restored=$true
    }finally{
        if($transcript){Stop-Transcript|Out-Null};$lock.Dispose()
        if($restored){Remove-Item -LiteralPath $lockPath}else{Write-Warning "Recovery required; lock retained for $RunDirectory"}
        $env:LOCALAPPDATA=$oldLocalAppData
        Write-Host "Hub run artifacts: $RunDirectory"
    }
}
