# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory)
$kit=Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria'
. "$kit\navigation.ps1" -RunDirectory $RunDirectory
$scenario=Get-Content -LiteralPath "$RunDirectory\scenario.json" -Raw|ConvertFrom-Json
function Assert-HubGameplay {
    $state=(Invoke-QaTool 'console.ps1' @{Command=@('mevr vehicle')})-join ' '
    if($state -notmatch ('map '+[regex]::Escape($scenario.expectedMap)+', game mode 0,') -or $state -notmatch 'BioVehicleBase: no'){
        throw "Scenario left expected on-foot gameplay: $state"
    }
}
function Check-HubPosition([string]$Name,[object]$Target,[double]$Tolerance){
    $p=Read-Position
    if([Math]::Sqrt([Math]::Pow($p[0]-$Target[0],2)+[Math]::Pow($p[1]-$Target[1],2)) -gt $Tolerance -or [Math]::Abs($p[2]-$Target[2]) -gt 65){
        throw "Checkpoint $Name failed: $p"
    }
    @{name=$Name;position=$p;target=$Target;time=(Get-Date).ToString('o');arrived=$true}|
        ConvertTo-Json -Compress|Add-Content -LiteralPath "$RunDirectory\waypoints.jsonl"
}
$started=Get-Date
try{
    Assert-HubGameplay
    $tolerance=if($scenario.start.positionToleranceUU){$scenario.start.positionToleranceUU}else{60}
    Check-HubPosition 'start' $scenario.start.positionUU $tolerance
    $difference=(($script:LastPawnYaw-$scenario.start.yawUnreal+98304)%65536)-32768
    $yawTolerance=if($scenario.start.yawToleranceUnreal){$scenario.start.yawToleranceUnreal}else{512}
    if([Math]::Abs($difference) -gt $yawTolerance){throw 'Prepared save facing differs from the measured route.'}
    foreach($step in $scenario.steps){
        $tolerance=if($step.toleranceUU){$step.toleranceUU}else{50}
        switch($step.type){
            'walk' {Walk-To $step.name $step.positionUU[0] $step.positionUU[1] $step.positionUU[2] $tolerance}
            'checkpoint' {Check-HubPosition $step.name $step.positionUU $tolerance}
            'look' {Look-At $step.yawDegrees $step.pitchDegrees}
            'pulse' {$stick=if($null -ne $step.stick){$step.stick}else{.55}; Walk-Pulse $step.yawDegrees $step.seconds $stick $step.pitchDegrees}
            'use' {& "$kit\use-door.ps1" -RunDirectory $RunDirectory}
            'wait' {Start-Sleep -Milliseconds ([int]($step.seconds*1000))}
            default {throw 'Unknown step; manifest validation must precede execution.'}
        }
        Assert-HubGameplay
    }
    Invoke-QaTool 'replay.ps1' @{Stop=$true}
    Look-At $scenario.capture.yawDegrees $scenario.capture.pitchDegrees
    $since=Get-Date
    Invoke-QaTool 'snapshot.ps1' @{Tag=($scenario.id+'-arrival')}
    $snapshot=Get-ChildItem -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\snapshots') -Filter '*.json'|
        Where-Object LastWriteTime -GE $since|Sort-Object LastWriteTime|Select-Object -Last 1
    if(-not $snapshot){throw 'Arrival snapshot missing.'}
    $meta=Get-Content -LiteralPath $snapshot.FullName -Raw|ConvertFrom-Json
    Copy-Item -LiteralPath $snapshot.FullName -Destination $RunDirectory
    foreach($name in $meta.files){Copy-Item -LiteralPath (Join-Path $snapshot.DirectoryName $name) -Destination $RunDirectory}
    @{completed=(Get-Date).ToString('o');started=$started.ToString('o');scenario=$scenario.id;
        map=$scenario.expectedMap;position=(Read-Position);scenarioSha256=$ctx.ScenarioSha256;routePassed=$true;performanceAccepted=$false}|
        ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$RunDirectory\route.json"
}catch{
    try{Invoke-QaTool 'replay.ps1' @{Stop=$true}}catch{Write-Warning $_}
    $_|Out-String|Set-Content -LiteralPath "$RunDirectory\route-error.txt"
    throw
}
