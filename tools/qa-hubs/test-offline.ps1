# SPDX-License-Identifier: MIT
param([string]$PythonExe='python')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$kit=Join-Path $repo 'tools\qa-cafeteria'
foreach($file in @((Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1').FullName)+@('launch-ready.ps1','navigation.ps1','use-door.ps1','capture.ps1'|ForEach-Object {Join-Path $kit $_})){
    $tokens=$null;$errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
    if($errors){throw ($errors|Out-String)}
}
$fixture=Join-Path $repo ('scratch\qa-hubs\offline-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $fixture|Out-Null
& $PythonExe "$PSScriptRoot\test-offline.py" $fixture
if($LASTEXITCODE){throw 'Offline hub tests failed.'}
& $PythonExe "$PSScriptRoot\test-recorded-route.py"
if($LASTEXITCODE){throw 'Recorded route offline checks failed.'}
& $PythonExe "$PSScriptRoot\test-save-fixture.py"
if($LASTEXITCODE){throw 'Save fixture checks failed.'}
$oldLocalAppData=$env:LOCALAPPDATA
try{
    # Exercise actual navigation math with mocked reads/pulses. No native call is reachable here.
    @{RepoRoot=$repo;PythonExe=$PythonExe;LocalAppData=$fixture;StartingYaw=-16164}|ConvertTo-Json|Set-Content -LiteralPath "$fixture\context.json"
    . "$kit\navigation.ps1" -RunDirectory $fixture
    function Read-Position {if($script:Walked){return $script:Target}else{return @(0,0,0)}}
    function Walk-Pulse([double]$Yaw,[double]$Seconds,[double]$Stick=.55,[double]$Pitch=0){$script:ObservedYaw=$Yaw;$script:Walked=$true}
    $script:Target=@(100,0,0);$script:Walked=$false
    Walk-To 'normandy-heading' 100 0 0 10
    if([Math]::Abs($script:ObservedYaw-88.79150390625) -gt .00001){throw 'Normandy heading default changed.'}
    $ctx.StartingYaw=16384;$script:Target=@(0,100,0);$script:Walked=$false
    Walk-To 'different-hub-heading' 0 100 0 10
    if([Math]::Abs($script:ObservedYaw) -gt .00001){throw 'Hub heading is not relative to the checked start yaw.'}
    # Exercise the actual Use gate against a fake common.ps1; no game command can escape the fixture.
    $mock=Join-Path $fixture 'mock';New-Item -ItemType Directory -Path $mock|Out-Null
    Copy-Item -LiteralPath "$kit\use-door.ps1" -Destination $mock
    @'
$ErrorActionPreference='Stop'
function Read-QaContext($unused){return @{StartingMap='BIOA_ICE00';RepoRoot='unused'}}
function Invoke-QaTool($name,$arguments){
 if($arguments.Command[0] -like 'mevr find*'){return $global:HubMockFound}
 $global:HubMockCalls++;return 'returned true'
}
'@|Set-Content -LiteralPath "$mock\common.ps1"
    $global:HubMockFound='[0] SFXGameModeDefault BIOA_ICE00.TheWorld.mode';$global:HubMockCalls=0
    & "$mock\use-door.ps1" -RunDirectory $fixture
    if($global:HubMockCalls -ne 1){throw 'Non-Normandy Use was not accepted.'}
    $global:HubMockFound='[0] SFXGameModeDefault BIOA_NOR00.TheWorld.mode';$global:HubMockCalls=0;$refused=$false
    try{& "$mock\use-door.ps1" -RunDirectory $fixture}catch{$refused=$true}
    if(-not $refused -or $global:HubMockCalls){throw 'Wrong-map Use was not rejected before the call.'}
}finally{
    $env:LOCALAPPDATA=$oldLocalAppData
    Remove-Variable HubMockFound,HubMockCalls -Scope Global -ErrorAction SilentlyContinue
}
"Hub offline checks passed. Native traversal and performance acceptance are separate. Fixture: $fixture"
