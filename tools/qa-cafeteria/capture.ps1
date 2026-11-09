# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory,[string]$Tag='cafeteria-high-turns')
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
. (Join-Path $ctx.RepoRoot 'tools\common.ps1')
$recording=Join-Path $RunDirectory 'cafeteria-turn.mevrrec'
& $ctx.PythonExe "$PSScriptRoot\movement.py" $recording --look-turn --seconds 30
if($LASTEXITCODE){throw 'Turn generation failed.'}
Invoke-QaTool 'replay.ps1' @{Path=$recording;Loop=$true;Fovs=$true}
Start-Sleep -Seconds 5
$since=Get-Date
$ini=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini'
$lastWrite=(Get-Item -LiteralPath $ini).LastWriteTimeUtc
$writeEvents=@()
[void](Write-MevrRequest 'perf.request' "seconds=90 delay=0 tag=$Tag")
$deadline=$since.AddSeconds(115);$found=$null
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
foreach($path in @($found.FullName,(Join-Path $found.DirectoryName $meta.files.frames),(Join-Path $found.DirectoryName $meta.files.ticks))){Copy-Item -LiteralPath $path -Destination $RunDirectory}
$hostLog=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
$rendererLog=Join-Path $ctx.LocalAppData 'MELEVR\MELEVR_Log.txt'
Copy-Item -LiteralPath $hostLog -Destination "$RunDirectory\host.log"
if(Test-Path -LiteralPath $rendererLog){Copy-Item -LiteralPath $rendererLog -Destination "$RunDirectory\renderer.log"}
& $ctx.PythonExe "$PSScriptRoot\analyze.py" $RunDirectory
if($LASTEXITCODE){throw 'Probe analysis failed.'}
