# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory)
. "$PSScriptRoot\navigation.ps1" -RunDirectory $RunDirectory
Invoke-QaTool 'console.ps1' @{Command=@('mevr omni close')}
Walk-To 'elevator-side-corridor' -5515 16135 -36740 40
Walk-To 'crew-corridor' -5710 15540 -36740 100
Walk-To 'mess-hall-entrance' -5710 15100 -36740
Walk-To 'mess-hall' -5500 15125 -36759
Look-At 90 0
$since=Get-Date
Invoke-QaTool 'snapshot.ps1' @{Tag='cafeteria-arrival'}
$snapshot=Get-ChildItem -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\snapshots') -Filter '*.json'|
    Where-Object {$_.LastWriteTime -ge $since}|Sort-Object LastWriteTime|Select-Object -Last 1
if(-not $snapshot){throw 'Arrival snapshot missing.'}
$meta=Get-Content -LiteralPath $snapshot.FullName -Raw|ConvertFrom-Json
Copy-Item -LiteralPath $snapshot.FullName -Destination $RunDirectory
foreach($name in $meta.files){Copy-Item -LiteralPath (Join-Path $snapshot.DirectoryName $name) -Destination $RunDirectory}
