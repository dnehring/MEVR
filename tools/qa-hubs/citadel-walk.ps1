# SPDX-License-Identifier: MIT
# Replays a private recorded Citadel walk in an explicitly selected, already-running LE1 session.
#Requires -Version 7.0
param(
    [ValidateSet('Prepare','Plan','BindStart','Run','Stop')][string]$Action='Plan',
    [string]$PackageDirectory='', [string]$RecordingPath='', [string]$ExpectedRecordingSha256='',
    [int]$GameProcessId=0, [string]$SaveFile='', [switch]$Capture,
    [ValidateRange(5,480)][int]$CaptureStartSeconds=45,
    [ValidateRange(1,120)][int]$CaptureSeconds=90,
    [string]$PythonExe='python',
    [string]$LocalAppData=[Environment]::GetFolderPath('LocalApplicationData')
)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $PackageDirectory){$PackageDirectory=Join-Path $repo 'scratch\qa-hubs\citadel-recorded-walk'}
$package=[IO.Path]::GetFullPath($PackageDirectory)
$routeHelper=Join-Path $PSScriptRoot 'recorded_route.py'
$toolsRoot=Join-Path $repo 'tools'
function Invoke-RoutePython([string[]]$Arguments){
    $output=& $PythonExe $routeHelper @Arguments
    if($LASTEXITCODE){throw "Route check failed: $($output -join ' ')"}
    return ($output -join "`n")|ConvertFrom-Json
}
if($Action -eq 'Prepare'){
    if(-not $RecordingPath -or $ExpectedRecordingSha256 -notmatch '^[a-fA-F0-9]{64}$'){
        throw 'Prepare needs -RecordingPath and its -ExpectedRecordingSha256.'
    }
    Invoke-RoutePython @('prepare',$RecordingPath,$package,'--expected-sha256',$ExpectedRecordingSha256)
    return
}
$route=Invoke-RoutePython @('verify',$package)
$fixturePath=Join-Path $package 'start.json'
$data=Join-Path $LocalAppData 'MEVR'
$lockPath=Join-Path $data 'qa-cafeteria.lock'
if($Action -eq 'Plan'){
    [ordered]@{package=$package;route=$route;startBound=(Test-Path -LiteralPath $fixturePath);
        captureWindow=@($CaptureStartSeconds,($CaptureStartSeconds+$CaptureSeconds));
        preparation='Reload the original save, use the same recenter/facing, then BindStart once; reload before each Run.';
        limits='Input replay candidate, no verified world route yet. Preserves recorded buttons. No runtime/settings changes.'}|
        ConvertTo-Json -Depth 8
    return
}
if($Action -eq 'Stop'){
    if(-not (Test-Path -LiteralPath $lockPath)){throw 'No active route lease.'}
    $run=Get-Content -LiteralPath $lockPath -Raw
    $owner=Get-Content -LiteralPath (Join-Path $run 'owner.json') -Raw|ConvertFrom-Json
    if($owner.kind -ne 'citadel-recorded-walk' -or $owner.package -ne $package){throw 'Lease belongs to a different test.'}
    Set-Content -LiteralPath (Join-Path $run 'stop.signal') -Value 'stop'
    Write-Host 'Stop requested. The running script will release replay inputs.'
    return
}
if($GameProcessId -le 0){throw 'Supply the already-running LE1 -GameProcessId. This script never launches a game.'}
$game=Get-Process -Id $GameProcessId
if($game.ProcessName -ne 'MassEffect1'){throw 'Selected process is not LE1.'}
$gameStarted=$game.StartTime.ToUniversalTime().Ticks
$bin=Split-Path $game.Path -Parent
function Assert-Game {
    $p=Get-Process -Id $GameProcessId
    if($p.ProcessName -ne 'MassEffect1' -or $p.StartTime.ToUniversalTime().Ticks -ne $gameStarted){throw 'Selected LE1 session ended or changed.'}
}
if($Capture -and ($CaptureStartSeconds+$CaptureSeconds+5 -gt $route.durationSeconds)){
    throw 'Capture window must end at least five seconds before the recording ends.'
}
if($Action -eq 'BindStart'){
    if(Test-Path -LiteralPath $fixturePath){throw 'Start already bound. Use a new package to bind a different save.'}
    if(-not $SaveFile -or [IO.Path]::GetExtension($SaveFile) -ne '.pcsav'){throw 'BindStart needs the original loaded .pcsav -SaveFile.'}
    $SaveFile=(Resolve-Path -LiteralPath $SaveFile).Path
}elseif(-not (Test-Path -LiteralPath $fixturePath)){throw 'Reload the original save and BindStart before Run.'}
New-Item -ItemType Directory -Path $data -Force|Out-Null
$lease=[IO.File]::Open($lockPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
$run=Join-Path $package ('runs\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$oldLocalAppData=$env:LOCALAPPDATA
$startedReplay=$false;$recoveryRequired=$false;$transcript=$false;$perfText=$null
$result=[ordered]@{action=$Action;completed=$false;nativeRouteValidated=$false;cleanPerformanceCapture=$false}
try{
    New-Item -ItemType Directory -Path $run|Out-Null
    $bytes=[Text.Encoding]::UTF8.GetBytes($run);$lease.Write($bytes,0,$bytes.Length);$lease.Flush()
    @{kind='citadel-recorded-walk';package=$package;pid=$GameProcessId;startedTicks=$gameStarted;scriptPid=$PID}|
        ConvertTo-Json|Set-Content -LiteralPath "$run\owner.json"
    Start-Transcript -LiteralPath "$run\transcript.txt"|Out-Null;$transcript=$true
    $env:LOCALAPPDATA=$LocalAppData
    . "$repo\tools\common.ps1"
    function Read-LiveStatus {
        Assert-Game
        $file=Get-Item -LiteralPath "$data\testloop.status"
        if(([datetime]::UtcNow-$file.LastWriteTimeUtc).TotalSeconds -gt 5){throw 'Test-loop status is stale.'}
        $s=Get-MevrTestLoopStatus
        if(-not $s -or -not $s.framesRunning){throw 'No live headset frames. Keep the VR session awake.'}
        return $s
    }
    function Read-World([string]$Name,[switch]$CheckStart){
        Assert-Game
        if(Test-Path -LiteralPath "$data\console.request"){throw 'Another console request is pending.'}
        $global:LASTEXITCODE=0
        $worldText=& "$toolsRoot\console.ps1" -Command @('mevr vehicle','mevr loc') -TimeoutSeconds 5
        if($LASTEXITCODE){throw 'World query failed.'}
        $path=Join-Path $run "$Name.txt"
        $worldText|Set-Content -LiteralPath $path
        $argsForWorld=@('world',$path)
        if($CheckStart){$argsForWorld+=@('--fixture',$fixturePath)}
        return Invoke-RoutePython $argsForWorld
    }
    if(@(Get-ChildItem -LiteralPath $data -Filter '*.request').Count){throw 'Another test-loop request is pending.'}
    $status=Read-LiveStatus
    if($status.recording -or $status.replaying){throw 'A recording or replay is already active.'}
    # Keep rerunnable source and configuration/build evidence with each attempt.
    New-Item -ItemType Directory -Path "$run\source\tools\qa-hubs","$run\config"|Out-Null
    foreach($file in @($PSCommandPath,$routeHelper)){
        Copy-Item -LiteralPath $file -Destination "$run\source\tools\qa-hubs"
    }
    foreach($name in @('mevrrec.py','common.ps1','console.ps1')){
        Copy-Item -LiteralPath (Join-Path $toolsRoot $name) -Destination "$run\source\tools"
    }
    $toolsRoot=Join-Path $run 'source\tools'
    $routeHelper=Join-Path $toolsRoot 'qa-hubs\recorded_route.py'
    @{Action=$Action;PackageDirectory=$package;GameProcessId=$GameProcessId;SaveFile=$SaveFile;Capture=[bool]$Capture;
        CaptureStartSeconds=$CaptureStartSeconds;CaptureSeconds=$CaptureSeconds;PythonExe=$PythonExe;LocalAppData=$LocalAppData}|
        ConvertTo-Json|Set-Content -LiteralPath "$run\invocation.json"
    $build=@(Get-ChildItem -LiteralPath "$bin\ASI" -File | Where-Object Name -in @('LE1MEVR.asi','LE1MEVR_mod.dll') |
        ForEach-Object {@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
    foreach($name in @('LE1MEVR.ini','MELEVR.ini')){
        $path=Join-Path "$bin\ASI" $name
        if(Test-Path -LiteralPath $path){Copy-Item -LiteralPath $path -Destination "$run\config"}
    }
    $runtime=Get-ItemPropertyValue 'HKLM:\SOFTWARE\Khronos\OpenXR\1' 'ActiveRuntime'
    @{buildFilesOnDisk=$build;runtime=$runtime;recordingSha256=$route.sha256;processStartedUtc=$game.StartTime.ToUniversalTime().ToString('o');
        note='On-disk hashes do not attest a module loaded before those files changed.'}|
        ConvertTo-Json -Depth 8|Set-Content -LiteralPath "$run\environment.json"
    if($Action -eq 'BindStart'){
        $start=Read-World 'start'
        $saveHash=(Get-FileHash -LiteralPath $SaveFile).Hash
        Copy-Item -LiteralPath $SaveFile -Destination "$package\starting-save.pcsav"
        if((Get-FileHash -LiteralPath "$package\starting-save.pcsav").Hash -ne $saveHash){throw 'Save changed during binding.'}
        @{schemaVersion=1;recordingSha256=$route.sha256;start=$start;saveFile=$SaveFile;saveSha256=$saveHash;
            selectionEvidence='Operator states this is the original freshly loaded save; engine slot identity is not exposed.';
            observed=(Get-Date).ToString('o')}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $fixturePath
        $result.completed=$true
        Write-Host 'Starting save and pose bound. Reload it before each replay.'
    }else{
        $fixture=Get-Content -LiteralPath $fixturePath -Raw|ConvertFrom-Json
        if($fixture.recordingSha256 -ne $route.sha256 -or
           (Get-FileHash -LiteralPath $fixture.saveFile).Hash -ne $fixture.saveSha256 -or
           (Get-FileHash -LiteralPath "$package\starting-save.pcsav").Hash -ne $fixture.saveSha256){throw 'Bound recording/save changed.'}
        $result.start=Read-World 'start' -CheckStart
        $clip=Join-Path $run 'walk.mevrrec'
        Copy-Item -LiteralPath "$package\walk.mevrrec" -Destination $clip
        if((Get-FileHash -LiteralPath $clip).Hash -ne $route.sha256){throw 'Recording changed before replay.'}
        $replayText="$clip inputs=1"
        $status=Read-LiveStatus
        if($status.replaying -or $status.recording -or (Test-Path -LiteralPath "$data\replay.request")){throw 'Test-loop ownership changed.'}
        $startedReplay=$true
        [void](Write-MevrRequest 'replay.request' $replayText)
        $deadline=(Get-Date).AddSeconds($route.durationSeconds+20)
        $ackDeadline=(Get-Date).AddSeconds(8)
        $seen=$false;$lastReplaySeconds=0;$probe=$null;$captureSince=$null
        $tag='citadel-walk-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')
        Write-Host 'Replaying the recorded Citadel walk once. Ctrl+C or -Action Stop requests release of inputs.'
        while((Get-Date) -lt $deadline){
            Start-Sleep -Milliseconds 500
            if(Test-Path -LiteralPath "$run\stop.signal"){throw 'Operator stopped the walk.'}
            $status=Read-LiveStatus
            if($status.replaying){
                if($status.replayPath -ne $clip -or -not $status.replayInputs -or $status.replayLoop){throw 'Replay ownership/options changed.'}
                $seen=$true;$lastReplaySeconds=$status.replaySeconds
                if($Capture -and -not $captureSince -and $lastReplaySeconds -ge $CaptureStartSeconds){
                    if(Test-Path -LiteralPath "$data\perf.request"){throw 'Another performance request is pending.'}
                    $captureSince=Get-Date
                    $perfText="seconds=$CaptureSeconds delay=0 tag=$tag"
                    [void](Write-MevrRequest 'perf.request' $perfText)
                    $result.captureRequestedAtReplaySeconds=$lastReplaySeconds
                    Write-Host "Measuring $CaptureSeconds seconds of the walk."
                }
            }elseif($seen){
                if($lastReplaySeconds -lt $route.durationSeconds-2){throw 'Replay stopped early.'}
                break
            }elseif((Get-Date) -gt $ackDeadline){throw 'Replay did not start.'}
            if($captureSince -and -not $probe){
                $probe=Get-ChildItem -LiteralPath "$data\perf" -Filter "*-$tag-probe.json" -ErrorAction SilentlyContinue|
                    Where-Object LastWriteTime -ge $captureSince|Select-Object -First 1
            }
        }
        if(-not $seen -or $status.replaying){throw 'Replay timed out.'}
        if($Capture){
            if(-not $probe){throw 'No completed timing capture for this walk.'}
            $meta=Get-Content -LiteralPath $probe.FullName -Raw|ConvertFrom-Json
            Copy-Item -LiteralPath $probe.FullName -Destination $run
            foreach($name in @($meta.files.frames,$meta.files.ticks,$meta.files.calls)|Where-Object {$_}){
                if([IO.Path]::GetFileName($name) -ne $name){throw 'Invalid probe artifact name.'}
                Copy-Item -LiteralPath (Join-Path $probe.DirectoryName $name) -Destination $run
            }
            $result.probe=$probe.Name
            $result.captureKind='Recorded traversal, no screenshots/world queries/replay replacements within the timing window. Route comparability pending.'
        }
        $result.end=Read-World 'end'
        $result.completed=$true
        Write-Host 'Replay completed. Check the actual path before treating it as a regression route.'
    }
}catch{
    $result.error=$_.Exception.Message
    throw
}finally{
    if($startedReplay){
        try{
            $request=Join-Path $data 'replay.request'
            # Even if the process crashed, never leave our unconsumed movement request for the next launch.
            if((Test-Path -LiteralPath $request) -and (Get-Content -LiteralPath $request -Raw) -eq $replayText){
                Remove-Item -LiteralPath $request
            }
            Assert-Game
            $s=Get-MevrTestLoopStatus
            if($s.replaying -and $s.replayPath -ne $clip){throw 'Another replay replaced this walk; refusing to stop its owner.'}
            if(Test-Path -LiteralPath $request){throw 'Another replay request is pending.'}
            [void](Write-MevrRequest 'replay.request' 'stop')
            $until=(Get-Date).AddSeconds(8)
            do{
                Start-Sleep -Milliseconds 250
                $s=Get-MevrTestLoopStatus
                $fresh=(Get-Item -LiteralPath "$data\testloop.status").LastWriteTimeUtc -gt [datetime]::UtcNow.AddSeconds(-5)
            }while((Get-Date) -lt $until -and ((Test-Path -LiteralPath $request) -or -not $s -or $s.replaying -or -not $fresh))
            if((Test-Path -LiteralPath $request) -or -not $s -or $s.replaying -or -not $fresh){throw 'Replay release was not acknowledged.'}
        }catch{
            $recoveryRequired=$true
            $result.releaseError=$_.Exception.Message
            Write-Warning "Replay release not verified; lease retained: $lockPath. Keep this receipt for recovery."
        }
    }
    if($perfText -and (Test-Path -LiteralPath "$data\perf.request") -and
       (Get-Content -LiteralPath "$data\perf.request" -Raw) -eq $perfText){Remove-Item -LiteralPath "$data\perf.request"}
    $result.recoveryRequired=$recoveryRequired
    if(Test-Path -LiteralPath $run){$result|ConvertTo-Json -Depth 10|Set-Content -LiteralPath "$run\result.json"}
    if($transcript){Stop-Transcript|Out-Null}
    $env:LOCALAPPDATA=$oldLocalAppData
    $lease.Dispose()
    if(-not $recoveryRequired){Remove-Item -LiteralPath $lockPath}
    Write-Host "Walk artifacts: $run"
}
if($recoveryRequired){throw 'Replay release needs recovery; inspect result.json before starting another test.'}
