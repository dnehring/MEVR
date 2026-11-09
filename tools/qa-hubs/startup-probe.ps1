# SPDX-License-Identifier: MIT
# Arm before launch, collect after Resume. Delay past the temporary Bink startup render thread.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,[Parameter(Mandatory)][ValidateSet('Arm','Collect','Cleanup')][string]$Action,
 [ValidateRange(15,30)][int]$Seconds=30,[ValidateRange(0,10)][int]$DelaySeconds=5)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$ctx=Read-QaContext $RunDirectory
. (Join-Path $ctx.RepoRoot 'tools\common.ps1')
$receiptPath=Join-Path $RunDirectory 'startup-request.json'
$requestPath=Join-Path $MevrDataDir 'perf.request'
if($Action -eq 'Arm'){
 if(Test-Path -LiteralPath $requestPath){throw 'A performance request already exists.'}
 $tag='startup-'+[IO.Path]::GetFileName($RunDirectory)
 $request="seconds=$Seconds delay=$DelaySeconds tag=$tag"
 @{requested=[datetime]::UtcNow.ToString('o');tag=$tag;seconds=$Seconds;delay=$DelaySeconds;text=$request;
   boundary='Bounded title segment; initial movie-thread handoff and Resume loading are outside this capture.'}|
  ConvertTo-Json|Set-Content -LiteralPath $receiptPath
 [void](Write-MevrRequest 'perf.request' $request)
 return
}
$receipt=Get-Content -LiteralPath $receiptPath -Raw|ConvertFrom-Json
if($Action -eq 'Cleanup'){
 # Never remove a request supplied by a different owner.
 if((Test-Path -LiteralPath $requestPath) -and (Get-Content -LiteralPath $requestPath -Raw).Trim() -eq $receipt.text){Remove-Item -LiteralPath $requestPath}
 return
}
$deadline=(Get-Date).AddSeconds($receipt.seconds+30);$found=$null
while((Get-Date) -lt $deadline -and -not $found){
 [void](Assert-QaOwnedGame)
 $found=Get-ChildItem -LiteralPath "$MevrDataDir\perf" -Filter "*-$($receipt.tag)-probe.json" -ErrorAction SilentlyContinue|
  Where-Object {$_.LastWriteTimeUtc -ge ([datetime]$receipt.requested).ToUniversalTime()}|Select-Object -Last 1
 if(-not $found){Start-Sleep -Milliseconds 500}
}
if(-not $found){throw 'Startup capture timed out.'}
$meta=Get-Content -LiteralPath $found.FullName -Raw|ConvertFrom-Json
Copy-Item -LiteralPath $found.FullName -Destination $RunDirectory
foreach($name in @($meta.files.frames,$meta.files.ticks,$meta.files.calls)|Where-Object {$_}){
 if([IO.Path]::GetFileName($name) -ne $name){throw 'Invalid probe artifact name.'}
 Copy-Item -LiteralPath (Join-Path $found.DirectoryName $name) -Destination $RunDirectory
}
Copy-Item -LiteralPath (Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\Logs\LE1MEVR.log') -Destination "$RunDirectory\host.log"
Copy-Item -LiteralPath (Join-Path $ctx.LocalAppData 'MELEVR\MELEVR_Log.txt') -Destination "$RunDirectory\renderer.log"
@{requested=$receipt.requested;completed=[datetime]::UtcNow.ToString('o');kind='startup';
  iniWriteMonitoring='not collected';note='Startup is an intentional transient; launch-events identifies test input and console queries.'}|
 ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\capture-window.json"
Write-Host "Startup capture collected: $($found.Name)"
