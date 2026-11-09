# SPDX-License-Identifier: MIT
# Resume cleanup after an interrupted run. Only the recorded game PID/start time may be closed.
param([Parameter(Mandatory)][string]$RunDirectory)
. "$PSScriptRoot\common.ps1"
$oldLocalAppData=$env:LOCALAPPDATA
try{
    $ctx=Read-QaContext $RunDirectory
    $lockPath=Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock'
    if(-not (Test-Path -LiteralPath $lockPath) -or [IO.File]::ReadAllText($lockPath) -ne $RunDirectory){throw 'Lock does not belong to this run.'}
    if(Get-Process MassEffect1 -ErrorAction SilentlyContinue){
        [void](Assert-QaOwnedGame)
        try{Invoke-QaTool 'replay.ps1' @{Stop=$true}}catch{Write-Warning $_}
        Invoke-QaTool 'console.ps1' @{Command=@('exit')}
        $until=(Get-Date).AddSeconds(60)
        while((Get-Process MassEffect1 -ErrorAction SilentlyContinue) -and (Get-Date) -lt $until){Start-Sleep -Milliseconds 500}
    }
    & "$PSScriptRoot\session.ps1" -Action Restore -RunDirectory $RunDirectory
    Remove-Item -LiteralPath $lockPath
}finally{$env:LOCALAPPDATA=$oldLocalAppData}
