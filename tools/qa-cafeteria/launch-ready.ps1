# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory,[int]$TimeoutSeconds=150,
      [ValidatePattern('^BIOA_[A-Z0-9]+$')][string]$ExpectedMap='BIOA_NOR00',
      [ValidateRange(0,90)][int]$StartupHoldSeconds=0,[switch]$ForegroundWindow)
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
if(Get-Process MassEffect1,MassEffectLauncher -ErrorAction SilentlyContinue){throw 'Game or launcher already running.'}
if(-not (Get-Process steam -ErrorAction SilentlyContinue)){throw 'Start Steam and sign in before this test.'}
if(-not ('CafeteriaKeys' -as [type])){
    Add-Type @'
using System; using System.Runtime.InteropServices;
public static class CafeteriaKeys { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l); }
'@
}
function Send-Key([IntPtr]$Window,[int]$Key){
    if($Window -eq [IntPtr]::Zero){return $false}
    [void][CafeteriaKeys]::PostMessage($Window,0x100,[IntPtr]$Key,[IntPtr]0)
    Start-Sleep -Milliseconds 80
    [void][CafeteriaKeys]::PostMessage($Window,0x101,[IntPtr]$Key,[IntPtr]0xC0000001)
    return $true
}
function Save-LaunchEvent([string]$Kind,[string]$Detail=''){
    @{utc=[datetime]::UtcNow.ToString('o');qpc=[Diagnostics.Stopwatch]::GetTimestamp();
      qpcFrequency=[Diagnostics.Stopwatch]::Frequency;kind=$Kind;detail=$Detail}|
        ConvertTo-Json -Compress|Add-Content -LiteralPath "$RunDirectory\launch-events.jsonl"
}
# Steam can leave launch waiting for an interstitial if asked during SteamVR startup.
for($i=0;$i -lt 30;$i++){
    $vr=Get-Process vrserver,vrmonitor -ErrorAction SilentlyContinue|Sort-Object StartTime -Descending|Select-Object -First 1
    if(-not $vr -or ((Get-Date)-$vr.StartTime).TotalSeconds -ge 15){break}
    Start-Sleep -Seconds 1
}
Save-LaunchEvent 'steam-launch-request'
Start-Process -FilePath (Join-Path $ctx.SteamRoot 'steam.exe') -WindowStyle Hidden -ArgumentList @('-applaunch','1328670','-game','1','-autoterminate')|Out-Null
$deadline=(Get-Date).AddSeconds(120);$lastKeys=Get-Date;$seen=$null;$gameProcess=$null
while((Get-Date) -lt $deadline){
    Start-Sleep -Seconds 2
    $gameProcess=Get-Process MassEffect1 -ErrorAction SilentlyContinue
    if($gameProcess){break}
    $launcher=Get-Process MassEffectLauncher -ErrorAction SilentlyContinue
    if($launcher -and -not $seen){$seen=Get-Date}
    if($launcher -and ((Get-Date)-$seen).TotalSeconds -gt 12 -and ((Get-Date)-$lastKeys).TotalSeconds -gt 10){
        $launcher.Refresh()
        if(Send-Key $launcher.MainWindowHandle 0x20){Start-Sleep -Milliseconds 900;[void](Send-Key $launcher.MainWindowHandle 0x0D);$lastKeys=Get-Date}
    }
}
if(-not $gameProcess){throw 'Steam/EA did not launch MassEffect1 within 120 seconds.'}
if($gameProcess.Path -ne (Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\MassEffect1.exe')){throw 'A different game installation launched.'}
@{pid=$gameProcess.Id;started=$gameProcess.StartTime.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath "$RunDirectory\owned-process.json"
Save-LaunchEvent 'game-observed' $gameProcess.StartTime.ToUniversalTime().ToString('o')
$log=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
$deadline=(Get-Date).AddSeconds($TimeoutSeconds);$enters=0;$lastEnter=Get-Date;$foregroundDone=$false;$lateForegroundDone=$false
while((Get-Date) -lt $deadline){
    Start-Sleep -Seconds 3
    [void](Assert-QaOwnedGame)
    if($ForegroundWindow -and -not $foregroundDone){
        $gameProcess.Refresh()
        if($gameProcess.MainWindowHandle -ne [IntPtr]::Zero){
            & (Join-Path $ctx.RepoRoot 'tools\qa-hubs\focus-owned-session.ps1') -RunDirectory $RunDirectory -Activate
            $foregroundDone=$true
        }
    }
    $item=Get-Item -LiteralPath $log -ErrorAction SilentlyContinue
    if(-not $item -or $item.LastWriteTime -lt $gameProcess.StartTime -or ((Get-Date)-$item.LastWriteTime).TotalSeconds -ge 8){continue}
    if($ForegroundWindow -and -not $lateForegroundDone -and ((Get-Date)-$gameProcess.StartTime).TotalSeconds -gt 25){
        # SteamVR can foreground its own window after the game's window first appeared.
        & (Join-Path $ctx.RepoRoot 'tools\qa-hubs\focus-owned-session.ps1') -RunDirectory $RunDirectory -Activate
        $lateForegroundDone=$true
    }
    if($enters -gt 0 -and -not ($ctx.StartupPhase -in @('Flight','Transition') -and $enters -eq 1)){
        # A console timeout while the level loads is expected; it removes its request.
        Save-LaunchEvent 'world-query-begin'
        $ready=(& (Join-Path $ctx.RepoRoot 'tools\console.ps1') 'mevr vehicle' -TimeoutSeconds 3)-join ' '
        Save-LaunchEvent 'world-query-end' $ready
        if($ready -match ('map '+[regex]::Escape($ExpectedMap)+', game mode 0,')){Save-LaunchEvent 'gameplay-ready' $ExpectedMap;Write-Host "READY: $ExpectedMap gameplay ($enters Enter presses)";return}
        if($ready -match 'map (\w+), game mode 0,'){throw "Wrong starting map: $($Matches[1])"}
    }
    $enterGap=if($ctx.StartupPhase -eq 'Flight' -and $enters -eq 1){$ctx.StartupSeconds+10}else{12}
    if(((Get-Date)-$gameProcess.StartTime).TotalSeconds -gt [Math]::Max(25,$StartupHoldSeconds) -and ((Get-Date)-$lastEnter).TotalSeconds -gt $enterGap){
        if(($ctx.StartupPhase -eq 'Splash' -and $enters -eq 0) -or ($ctx.StartupPhase -eq 'Flight' -and $enters -eq 1)){
            # A probe bound to a temporary render thread never completes. Do not carry it into another phase.
            $probeRequest=Get-Content -LiteralPath "$RunDirectory\startup-request.json" -Raw|ConvertFrom-Json
            $finished=Get-ChildItem -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\perf') -Filter "*-$($probeRequest.tag)-probe.json" -ErrorAction SilentlyContinue
            if(-not $finished){
                if(($ctx.StartupPhase -eq 'Splash' -and ((Get-Date)-$gameProcess.StartTime).TotalSeconds -gt 95) -or
                   ($ctx.StartupPhase -eq 'Flight' -and ((Get-Date)-$lastEnter).TotalSeconds -gt $ctx.StartupSeconds+30)){
                    throw 'Title capture did not finish before the next transition; refusing a cross-thread capture.'
                }
                continue
            }
        }
        $gameProcess.Refresh()
        if($ForegroundWindow -and $enters -eq 0){
            & (Join-Path $ctx.RepoRoot 'tools\qa-hubs\focus-owned-session.ps1') -RunDirectory $RunDirectory -Activate
        }
        if($ctx.StartupPhase -eq 'Flight' -and $enters -eq 0){
            & (Join-Path $ctx.RepoRoot 'tools\qa-hubs\startup-probe.ps1') -RunDirectory $RunDirectory -Action Arm -Seconds $ctx.StartupSeconds -DelaySeconds 0
        }
        if(Send-Key $gameProcess.MainWindowHandle 0x0D){$enters++;$lastEnter=Get-Date;Save-LaunchEvent 'enter-posted' "$enters"}
    }
}
throw "Timed out waiting for the prepared $ExpectedMap save."
