# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory,[string]$Tag='cafeteria-high-turns',
      [ValidateRange(30,120)][int]$Seconds=90,[string]$RecordingPath='', [switch]$ReplayAlreadyRunning)
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
. (Join-Path $ctx.RepoRoot 'tools\common.ps1')
$recording=$RecordingPath
if(-not $recording){
    $recording=Join-Path $RunDirectory 'cafeteria-turn.mevrrec'
    & $ctx.PythonExe "$PSScriptRoot\movement.py" $recording --look-turn --seconds 30
    if($LASTEXITCODE){throw 'Turn generation failed.'}
}
if(-not (Test-Path -LiteralPath $recording)){throw 'Turn recording is missing.'}
if($ReplayAlreadyRunning){
    $status=Get-MevrTestLoopStatus
    $updated=(Get-Item -LiteralPath "$MevrDataDir\testloop.status").LastWriteTimeUtc
    if(-not $status.replaying -or -not $status.framesRunning -or $status.replayPath -ne [IO.Path]::GetFullPath($recording) -or
       ([datetime]::UtcNow-$updated).TotalSeconds -gt 5){throw 'The expected settled replay is not active.'}
}else{Invoke-QaTool 'replay.ps1' @{Path=$recording;Loop=$true;Fovs=$true}}
Start-Sleep -Seconds 5
$since=Get-Date
$ini=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini'
$lastWrite=(Get-Item -LiteralPath $ini).LastWriteTimeUtc
$writeEvents=@()
[void](Write-MevrRequest 'perf.request' "seconds=$Seconds delay=0 tag=$Tag")
$deadline=$since.AddSeconds($Seconds+25);$found=$null
while((Get-Date) -lt $deadline -and -not $found){
    Start-Sleep -Milliseconds 500
    [void](Assert-QaOwnedGame)
    $observed=(Get-Item -LiteralPath $ini).LastWriteTimeUtc
    if($observed -ne $lastWrite){$writeEvents+=@{observed=(Get-Date).ToString('o');fileWriteUtc=$observed.ToString('o')};$lastWrite=$observed}
    $found=Get-ChildItem -LiteralPath "$MevrDataDir\perf" -Filter "*-$Tag-probe.json" -ErrorAction SilentlyContinue|
        Where-Object {$_.LastWriteTime -ge $since}|Sort-Object LastWriteTime|Select-Object -Last 1
}
if(-not $found){throw 'Capture timed out.'}
@{requested=$since.ToString('o');completed=(Get-Date).ToString('o');iniWrites=$writeEvents}|
    ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$RunDirectory\capture-window.json"
$meta=Get-Content -LiteralPath $found.FullName -Raw|ConvertFrom-Json
$artifacts=@($found.FullName,(Join-Path $found.DirectoryName $meta.files.frames),(Join-Path $found.DirectoryName $meta.files.ticks))
if($meta.files.calls){$artifacts+=Join-Path $found.DirectoryName $meta.files.calls}
foreach($path in $artifacts){Copy-Item -LiteralPath $path -Destination $RunDirectory}
$hostLog=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
$rendererLog=Join-Path $ctx.LocalAppData 'MELEVR\MELEVR_Log.txt'
Copy-Item -LiteralPath $hostLog -Destination "$RunDirectory\host.log"
if(Test-Path -LiteralPath $rendererLog){Copy-Item -LiteralPath $rendererLog -Destination "$RunDirectory\renderer.log"}
& $ctx.PythonExe "$PSScriptRoot\analyze.py" $RunDirectory
if($LASTEXITCODE){throw 'Probe analysis failed.'}
