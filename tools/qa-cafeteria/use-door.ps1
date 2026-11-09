# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory)
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
$repo=$ctx.RepoRoot
# Invoke the normal gameplay Use action after the replay has selected the door.
$ErrorActionPreference='Stop'
$found=Invoke-QaTool 'console.ps1' @{Command=@('mevr find SFXGameModeDefault_')}
$expectedMap=if($ctx.StartingMap){$ctx.StartingMap}else{'BIOA_NOR00'}
if($found -notmatch ('\[0\] SFXGameModeDefault '+[regex]::Escape($expectedMap)+'\.')) { throw 'Unexpected active game mode; refusing Use.' }
$result=Invoke-QaTool 'console.ps1' @{Command=@('mevr call 0 Used')}
$result
$result | Add-Content -LiteralPath "$RunDirectory\route.log"
if($result -notmatch 'returned true') { throw 'No usable door selected.' }
@{step='use-door';time=(Get-Date).ToString('o');action='normal-gameplay-Used'} | ConvertTo-Json -Compress | Add-Content -LiteralPath "$RunDirectory\steps.jsonl"
