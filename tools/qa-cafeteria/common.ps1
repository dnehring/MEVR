# SPDX-License-Identifier: MIT
# Shared context and checked calls. No game/configuration changes when dot-sourced.
$ErrorActionPreference='Stop'
function Read-QaContext([string]$RunDirectory) {
    $ctx=Get-Content -LiteralPath (Join-Path $RunDirectory 'context.json') -Raw | ConvertFrom-Json
    $env:LOCALAPPDATA=$ctx.LocalAppData
    return $ctx
}
function Invoke-QaTool([string]$Name,[hashtable]$Arguments=@{}) {
    $global:LASTEXITCODE=0
    & (Join-Path $ctx.RepoRoot "tools\$Name") @Arguments
    if($LASTEXITCODE -ne 0){throw "$Name failed with exit code $LASTEXITCODE"}
}
function Get-QaRuntime {
    Get-ItemPropertyValue 'HKLM:\SOFTWARE\Khronos\OpenXR\1' 'ActiveRuntime'
}
function Get-QaProcesses {
    @(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher,vrmonitor,vrserver,vrcompositor,vrdashboard,vrwebhelper,vrstartup -ErrorAction SilentlyContinue)
}
function Find-QaInstall([string]$SteamRoot,[string]$AppId) {
    $libs=@($SteamRoot)
    $vdf=Join-Path $SteamRoot 'steamapps\libraryfolders.vdf'
    if(Test-Path -LiteralPath $vdf){
        $libs+=@(Select-String -LiteralPath $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object {$_.Matches[0].Groups[1].Value -replace '\\\\','\'})
    }
    foreach($lib in $libs|Select-Object -Unique){
        $manifest=Join-Path $lib "steamapps\appmanifest_$AppId.acf"
        if(Test-Path -LiteralPath $manifest){
            $name=(Select-String -LiteralPath $manifest -Pattern '"installdir"\s+"([^"]+)"').Matches[0].Groups[1].Value
            return Join-Path $lib "steamapps\common\$name"
        }
    }
    throw "Steam app $AppId not found; supply its installation path explicitly."
}
function Assert-QaOwnedGame {
    $receipt=Get-Content -LiteralPath (Join-Path $RunDirectory 'owned-process.json') -Raw|ConvertFrom-Json
    $gameProcess=Get-Process -Id $receipt.pid -ErrorAction Stop
    if($gameProcess.ProcessName -ne 'MassEffect1' -or -not (Test-QaProcessIdentity $gameProcess $receipt)){throw 'The owned game process ended or changed.'}
    return $gameProcess
}
function Test-QaProcessIdentity([object]$Process,[object]$Receipt){
    # PS 7.5+ converts ISO JSON strings into DateTime. Compare instants, not their formatted strings.
    return $Process.Id -eq $Receipt.pid -and $Process.StartTime.ToUniversalTime().Ticks -eq ([datetime]$Receipt.started).ToUniversalTime().Ticks
}
