# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][string]$RunDirectory,[int]$TimeoutSeconds=150,
      [ValidatePattern('^BIOA_[A-Z0-9]+$')][string]$ExpectedMap='BIOA_NOR00')
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
# Steam can leave launch waiting for an interstitial if asked during SteamVR startup.
for($i=0;$i -lt 30;$i++){
    $vr=Get-Process vrserver,vrmonitor -ErrorAction SilentlyContinue|Sort-Object StartTime -Descending|Select-Object -First 1
    if(-not $vr -or ((Get-Date)-$vr.StartTime).TotalSeconds -ge 15){break}
    Start-Sleep -Seconds 1
}
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
$log=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\Logs\LE1MEVR.log'
$deadline=(Get-Date).AddSeconds($TimeoutSeconds);$enters=0;$lastEnter=Get-Date
while((Get-Date) -lt $deadline){
    Start-Sleep -Seconds 3
    [void](Assert-QaOwnedGame)
    $item=Get-Item -LiteralPath $log -ErrorAction SilentlyContinue
    if(-not $item -or $item.LastWriteTime -lt $gameProcess.StartTime -or ((Get-Date)-$item.LastWriteTime).TotalSeconds -ge 8){continue}
    if($enters -gt 0){
        # A console timeout while the level loads is expected; it removes its request.
        $ready=(& (Join-Path $ctx.RepoRoot 'tools\console.ps1') 'mevr vehicle' -TimeoutSeconds 3)-join ' '
        if($ready -match ('map '+[regex]::Escape($ExpectedMap)+', game mode 0,')){Write-Host "READY: $ExpectedMap gameplay ($enters Enter presses)";return}
        if($ready -match 'map (\w+), game mode 0,'){throw "Wrong starting map: $($Matches[1])"}
    }
    if(((Get-Date)-$gameProcess.StartTime).TotalSeconds -gt 25 -and ((Get-Date)-$lastEnter).TotalSeconds -gt 12){
        $gameProcess.Refresh()
        if(Send-Key $gameProcess.MainWindowHandle 0x0D){$enters++;$lastEnter=Get-Date}
    }
}
throw "Timed out waiting for the prepared $ExpectedMap save."
