# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory,[string]$Tag='cafeteria-high')
. "$PSScriptRoot\navigation.ps1" -RunDirectory $RunDirectory
$p=Read-Position
$yawDifference=(($script:LastPawnYaw+16164+98304)%65536)-32768
if([Math]::Abs($yawDifference) -gt 512){throw 'Saved starting facing changed; refusing the fixed-heading route.'}
if(([Math]::Abs($p[0]+5170.5) -gt 60 -or [Math]::Abs($p[1]-14705.2) -gt 60 -or [Math]::Abs($p[2]+36047.9) -gt 60)){throw "Load the original CIC save first; found $p"}
$started=Get-Date
try {
  Walk-To 'ramp-start' -5175 14958 -36086
  Walk-To 'ramp-side' -5246 14958 -36084 35
 Walk-To 'ramp-foot' -5250 15200 -36148
 Walk-To 'side-walkway' -5940 15200 -36168
 Walk-To 'guard-approach' -5940 14790 -36187
 Walk-To 'door-approach' -6240 14790 -36168
 Look-At 215 -10
 & "$PSScriptRoot\use-door.ps1" -RunDirectory $RunDirectory
 Walk-To 'inside-stairwell' -6240 15400 -36260
 Walk-To 'outer-stair' -6400 15400 -36260
 Walk-To 'stair-turn' -6400 15840 -36384
 # Cardinal legs follow the curved collision rail; direct diagonal targets cut its corner.
 Walk-Pulse 90 10 .55
 Walk-Pulse 180 9 .55
 Walk-Pulse 90 13 .55
 $lobby=Read-Position
 if([Math]::Abs($lobby[0]+5200) -gt 150 -or [Math]::Abs($lobby[1]-16400) -gt 150 -or [Math]::Abs($lobby[2]+36721) -gt 50){throw "Stair descent failed: $lobby"}
 Look-At 0 0
 & "$PSScriptRoot\to-cafeteria.ps1" -RunDirectory $RunDirectory
 Invoke-QaTool 'console.ps1' @{Command=@('mevr vehicle')}
 @{completed=(Get-Date).ToString('o');started=$started.ToString('o');tag=$Tag;position=(Read-Position)} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$RunDirectory\route-$Tag.json"
} catch {
 try{Invoke-QaTool 'replay.ps1' @{Stop=$true}}catch{Write-Warning $_}
 $_ | Out-String | Set-Content -LiteralPath "$RunDirectory\route-$Tag-error.txt"
 throw
}
