# SPDX-License-Identifier: MIT
# Extra, intrusive diagnostic capture. Never include its timing in the clean baseline.
param([Parameter(Mandatory)][string]$RunDirectory,[ValidateRange(1,3)][int]$Captures=2)
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
. (Join-Path $ctx.RepoRoot 'tools\common.ps1')
[void](Assert-QaOwnedGame)
$sampler=Join-Path $ctx.RepoRoot 'build\Release\mevr-stack-sampler.exe'
if(-not (Test-Path -LiteralPath $sampler)){throw 'Build mevr-stack-sampler in build/Release first.'}
$since=Get-Date
[void](Write-MevrRequest 'perf.request' 'seconds=2 delay=0 tag=cafeteria-thread-id')
$deadline=$since.AddSeconds(15);$found=$null
while((Get-Date) -lt $deadline -and -not $found){
    Start-Sleep -Milliseconds 500
    $found=Get-ChildItem -LiteralPath "$MevrDataDir\perf" -Filter '*-cafeteria-thread-id-probe.json' -ErrorAction SilentlyContinue|
        Where-Object {$_.LastWriteTime -ge $since}|Sort-Object LastWriteTime|Select-Object -Last 1
}
if(-not $found){throw 'Could not discover the game thread.'}
$meta=Get-Content -LiteralPath $found.FullName -Raw|ConvertFrom-Json
$owned=Assert-QaOwnedGame
if($meta.pid -ne $owned.Id){throw 'Thread probe belongs to another process.'}
@{intrusive=$true;captures=$Captures;gamePid=$owned.Id;gameThread=$meta.gameThread}|
    ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\diagnostic.json"
for($index=1;$index -le $Captures;$index++){
    $sampleDir=Join-Path $RunDirectory "diagnostic-$index"
    New-Item -ItemType Directory -Path $sampleDir|Out-Null
    foreach($name in 'context.json','owned-process.json'){Copy-Item -LiteralPath (Join-Path $RunDirectory $name) -Destination $sampleDir}
    $raw=Join-Path $sampleDir 'game-thread-stacks.txt';$times=Join-Path $sampleDir 'sample-times.csv'
    $args=@([string]$owned.Id,[string]$meta.gameThread,'110','10000',('"{0}"' -f $raw),('"{0}"' -f $times))
    $process=Start-Process -FilePath $sampler -ArgumentList $args -WindowStyle Hidden -PassThru -RedirectStandardOutput "$sampleDir\sampler-summary.txt" -RedirectStandardError "$sampleDir\sampler-errors.txt"
    @{started=(Get-Date).ToString('o');gamePid=$owned.Id;gameThread=$meta.gameThread;samplerPid=$process.Id;intervalUs=10000;
        samplerSha256=(Get-FileHash -LiteralPath $sampler).Hash;intrusive=$true;reason='Suspends the game thread for each sample; timing is diagnostic only.'}|
        ConvertTo-Json|Set-Content -LiteralPath "$sampleDir\diagnostic.json"
    try{
        # Initial symbol/stack loading can be expensive; exclude warmup from the perf probe.
        Start-Sleep -Seconds 7
        & "$PSScriptRoot\capture.ps1" -RunDirectory $sampleDir -Tag "cafeteria-$($ctx.Preset.ToLower())-traced-$index"
        if(-not $process.WaitForExit(60000)){throw 'Sampler did not finish.'}
        if($process.ExitCode -ne 0){throw "Sampler failed: $($process.ExitCode)"}
    }finally{
        if(-not $process.HasExited){Stop-Process -Id $process.Id -Force}
    }
}
