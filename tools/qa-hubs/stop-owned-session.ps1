# SPDX-License-Identifier: MIT
# Gracefully abort only the game owned by the named active QA session. Its controller performs restoration.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,[Parameter(Mandatory)][string]$Reason)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$RunDirectory=[IO.Path]::GetFullPath($RunDirectory)
$ctx=Read-QaContext $RunDirectory
$lease=Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock'
if((Get-Content -LiteralPath $lease -Raw) -ne $RunDirectory){throw 'A different session owns the lease.'}
$owner=Get-Content -LiteralPath "$RunDirectory\session-owner.json" -Raw|ConvertFrom-Json
$controller=Get-Process -Id $owner.scriptPid -ErrorAction Stop
if($controller.StartTime.ToUniversalTime().Ticks -ne $owner.scriptStartedTicks){throw 'The restoring controller is no longer active.'}
[void](Assert-QaOwnedGame)
@{requested=[datetime]::UtcNow.ToString('o');reason=$Reason}|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\abort-request.json"
Invoke-QaTool 'console.ps1' @{Command=@('exit')}
Write-Host 'Exit requested. Wait for the session controller to restore and release its lease.'
