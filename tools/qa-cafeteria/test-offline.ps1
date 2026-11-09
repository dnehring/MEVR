# SPDX-License-Identifier: MIT
# No game/registry/SteamVR changes: tests use a fresh ignored fixture directory.
param([string]$PythonExe='python')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
foreach($file in @((Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1').FullName)+(Join-Path $repo 'tools\fake-headset.ps1')){
    $tokens=$null;$errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
    if($errors){throw ($errors|Out-String)}
}
. "$PSScriptRoot\state.ps1"
. "$PSScriptRoot\common.ps1"
$process=Get-Process -Id $PID
$identity=@{pid=$process.Id;started=$process.StartTime.ToString('o')}|ConvertTo-Json|ConvertFrom-Json
if(-not (Test-QaProcessIdentity $process $identity)){throw 'Process identity failed its JSON timestamp round trip.'}
$identity.pid=-1
if(Test-QaProcessIdentity $process $identity){throw 'Wrong process identity was accepted.'}
$fixture=Join-Path $repo ('scratch\qa-cafeteria\offline-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$live=Join-Path $fixture 'live';$run=Join-Path $fixture 'run'
New-Item -ItemType Directory -Path $live,$run|Out-Null
$original=Join-Path $live 'save.txt';$deleted=Join-Path $live 'settings.txt';$absent=Join-Path $fixture 'marker'
[IO.File]::WriteAllText($original,'original save');[IO.File]::WriteAllText($deleted,'original settings')
Save-QaBackup $run @($absent) @($live) 'fixture-binary' 'fixture-runtime'
$m=Get-Content -LiteralPath "$run\before.json" -Raw|ConvertFrom-Json
$refused=$false
try{Save-QaBackup $run @($absent) @($live) 'fixture-binary' 'fixture-runtime'}catch{$refused=$true}
if(-not $refused){throw 'Backup replacement was not refused.'}
[IO.File]::WriteAllText($original,'test save');Remove-Item -LiteralPath $deleted
[IO.File]::WriteAllText($absent,'test marker');[IO.File]::WriteAllText((Join-Path $live 'new-autosave.txt'),'test autosave')
# Damaged backups must fail before any live file changes.
$saved=($m.files|Where-Object path -eq $original).saved
$bytes=[IO.File]::ReadAllBytes($saved);[IO.File]::WriteAllText($saved,'corrupt')
$refused=$false
try{Restore-QaBackup $run}catch{$refused=$true}
if(-not $refused -or [IO.File]::ReadAllText($original) -ne 'test save'){throw 'Damaged backup guard failed.'}
[IO.File]::WriteAllBytes($saved,$bytes)
Restore-QaBackup $run
if(@(Test-QaBackup $m).Count -or (Test-Path -LiteralPath (Join-Path $live 'new-autosave.txt'))){throw 'Fixture restoration failed.'}
$receipt=Get-Content -LiteralPath "$run\restored.json" -Raw|ConvertFrom-Json
if($receipt.changed.Count -ne 4){throw 'Changed/generated files were not all archived.'}
& $PythonExe "$PSScriptRoot\test-recordings.py" $fixture
if($LASTEXITCODE){throw 'Recording checks failed.'}
"Offline checks passed. Fixture: $fixture"
