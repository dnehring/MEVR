# SPDX-License-Identifier: MIT
# Exercises the real runner with an isolated fake filesystem/test loop. No native game calls.
#Requires -Version 7.0
param([string]$PythonExe='python')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root=Join-Path $repo ('scratch\qa-hubs\replay-offline-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$mockTools=Join-Path $root 'source\tools'
New-Item -ItemType Directory -Path "$mockTools\qa-hubs","$root\local\MEVR","$root\game\ASI"|Out-Null
Copy-Item -LiteralPath "$PSScriptRoot\citadel-walk.ps1","$PSScriptRoot\recorded_route.py" -Destination "$mockTools\qa-hubs"
Copy-Item -LiteralPath "$repo\tools\mevrrec.py" -Destination $mockTools
@'
$MevrDataDir=Join-Path $env:LOCALAPPDATA 'MEVR'
function Get-MevrTestLoopStatus {
 if($global:ReplayMock.active){
  $global:ReplayMock.seconds+=10
  if($global:ReplayMock.early -or $global:ReplayMock.seconds -gt 39){$global:ReplayMock.active=$false}
 }
 $s=@{recording=$false;replaying=$global:ReplayMock.active;replayPath=$global:ReplayMock.path;
      replaySeconds=[Math]::Min(29.9,$global:ReplayMock.seconds);replayInputs=$true;replayLoop=$false;framesRunning=$true}
 $s|ConvertTo-Json|Set-Content -LiteralPath "$MevrDataDir\testloop.status"
 return [pscustomobject]$s
}
function Write-MevrRequest($name,$text){
 if($name -eq 'perf.request'){
  $tag=($text -split 'tag=')[1]
  New-Item -ItemType Directory -Path "$MevrDataDir\perf" -Force|Out-Null
  foreach($file in @('frames.csv','ticks.csv','calls.csv')){'offline mock'|Set-Content -LiteralPath "$MevrDataDir\perf\$file"}
  @{files=@{frames='frames.csv';ticks='ticks.csv';calls='calls.csv'}}|ConvertTo-Json|
   Set-Content -LiteralPath "$MevrDataDir\perf\mock-$tag-probe.json"
  return Join-Path $MevrDataDir $name
 }
 if($name -ne 'replay.request'){throw 'Unexpected mock request'}
 $global:ReplayMock.requests++
 if($text -eq 'stop'){$global:ReplayMock.active=$false;$global:ReplayMock.stops++}
 else{
  $global:ReplayMock.active=$true;$global:ReplayMock.path=$text -replace ' inputs=1$','';$global:ReplayMock.seconds=-9
  if($global:ReplayMock.operatorStop){
   & $global:ReplayMock.runner -Action Stop -PackageDirectory $global:ReplayMock.package -PythonExe $global:ReplayMock.python -LocalAppData $env:LOCALAPPDATA
  }
 }
 return Join-Path $MevrDataDir $name
}
'@|Set-Content -LiteralPath "$mockTools\common.ps1"
@'
param($Command,$TimeoutSeconds)
if($global:ReplayMock.wrongMap){'map BIOA_NOR00, game mode 0, pawn X (BioVehicleBase: no)'}
else{'map BIOA_STA00, game mode 0, pawn X (BioVehicleBase: no)'}
'loc 1 2 3 rot 0 0 0'
'@|Set-Content -LiteralPath "$mockTools\console.ps1"
$runner=Join-Path $mockTools 'qa-hubs\citadel-walk.ps1'
$package=Join-Path $root 'package'
& $PythonExe "$PSScriptRoot\turn.py" "$root\clip.mevrrec"
if($LASTEXITCODE){throw 'Could not create offline clip'}
$sha=(Get-FileHash -LiteralPath "$root\clip.mevrrec").Hash
& $runner -Action Prepare -RecordingPath "$root\clip.mevrrec" -ExpectedRecordingSha256 $sha -PackageDirectory $package -PythonExe $PythonExe|Out-Null
$global:ReplayMock=@{active=$false;seconds=0;path='';requests=0;stops=0;early=$false;wrongMap=$false}
$global:ReplayMock.runner=$runner;$global:ReplayMock.package=$package;$global:ReplayMock.python=$PythonExe
$mockGamePath=Join-Path $root 'game\MassEffect1.exe'
function Get-Process {param($Id) return [pscustomobject]@{Id=$Id;ProcessName='MassEffect1';Path=$mockGamePath;StartTime=[datetime]'2026-11-08T01:00:00Z'}}
function Get-ItemPropertyValue {param($Path,$Name) return 'offline-mock-runtime'}
function Start-Sleep {param($Milliseconds)}
$oldLocal=$env:LOCALAPPDATA
try{
    $env:LOCALAPPDATA=Join-Path $root 'local'
    . "$mockTools\common.ps1"
    [void](Get-MevrTestLoopStatus)
    'synthetic private save'|Set-Content -LiteralPath "$root\save.pcsav"
    $arguments=@{PackageDirectory=$package;GameProcessId=424242;PythonExe=$PythonExe;LocalAppData=$env:LOCALAPPDATA}
    (Get-Item -LiteralPath "$env:LOCALAPPDATA\MEVR\testloop.status").LastWriteTimeUtc=[datetime]::UtcNow.AddSeconds(-20)
    & $runner @arguments -Action BindStart -SaveFile "$root\save.pcsav"
    [void](Get-MevrTestLoopStatus)
    # The live source slot may be overwritten or deleted after binding; only the archive is authoritative.
    Remove-Item -LiteralPath "$root\save.pcsav"
    & $runner @arguments -Action Run
    if($global:ReplayMock.stops -ne 1 -or $global:ReplayMock.active){throw 'Normal completion did not release replay'}
    & $runner @arguments -Action Run -Capture -CaptureStartSeconds 5 -CaptureSeconds 10
    $captureRun=Get-ChildItem -LiteralPath "$package\runs" -Directory|Sort-Object Name|Select-Object -Last 1
    $captured=Get-Content -LiteralPath "$($captureRun.FullName)\result.json" -Raw|ConvertFrom-Json
    if(-not $captured.completed -or -not $captured.probe -or -not (Test-Path -LiteralPath "$($captureRun.FullName)\calls.csv")){
        throw 'Probe metadata or queued-call artifact was lost'
    }
    $global:ReplayMock.operatorStop=$true;$refused=$false
    try{& $runner @arguments -Action Run}catch{$refused=$_.Exception.Message -match 'Operator stopped'}
    if(-not $refused -or $global:ReplayMock.active){throw 'Operator Stop failed'}
    $global:ReplayMock.operatorStop=$false
    $global:ReplayMock.early=$true;$refused=$false
    try{& $runner @arguments -Action Run}catch{$refused=$_.Exception.Message -match 'did not start|stopped early'}
    if(-not $refused -or $global:ReplayMock.stops -ne 4 -or $global:ReplayMock.active){throw 'Early-stop recovery failed'}
    $global:ReplayMock.early=$false;$global:ReplayMock.wrongMap=$true;$requests=$global:ReplayMock.requests;$refused=$false
    try{& $runner @arguments -Action Run}catch{$refused=$_.Exception.Message -match 'Route check failed'}
    if(-not $refused -or $global:ReplayMock.requests -ne $requests){throw 'Wrong map reached replay'}
    if(Test-Path -LiteralPath "$env:LOCALAPPDATA\MEVR\qa-cafeteria.lock"){throw 'Successful recovery left a lease'}
    $global:ReplayMock.wrongMap=$false
    $session=Join-Path $root 'automatic-session'
    New-Item -ItemType Directory -Path $session|Out-Null
    $lease=[IO.File]::Open("$env:LOCALAPPDATA\MEVR\qa-cafeteria.lock",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{
        $bytes=[Text.Encoding]::UTF8.GetBytes($session);$lease.Write($bytes,0,$bytes.Length);$lease.Flush()
        @{kind='citadel-recorded-walk';package=$package;scriptPid=$PID;gamePid=424242;
          gameStartedTicks=([datetime]'2026-11-08T01:00:00Z').ToUniversalTime().Ticks;childRun=$null}|
          ConvertTo-Json|Set-Content -LiteralPath "$session\session-owner.json"
        & $runner @arguments -Action Run -SessionDirectory $session
        if(-not (Test-Path -LiteralPath "$env:LOCALAPPDATA\MEVR\qa-cafeteria.lock")){throw 'Child removed the automatic session lease'}
    }finally{$lease.Dispose();Remove-Item -LiteralPath "$env:LOCALAPPDATA\MEVR\qa-cafeteria.lock"}
}finally{
    $env:LOCALAPPDATA=$oldLocal
    Remove-Variable ReplayMock -Scope Global -ErrorAction SilentlyContinue
}
Write-Host "Citadel replay lifecycle checks passed. Artifacts: $root"
