# SPDX-License-Identifier: MIT
param([Parameter(Mandatory)][ValidateSet('Prepare','Restore','Verify')][string]$Action,
      [Parameter(Mandatory)][string]$RunDirectory)
. "$PSScriptRoot\common.ps1"
. "$PSScriptRoot\state.ps1"
$ctx=Read-QaContext $RunDirectory
$asi=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\ASI'
$binary=Join-Path $asi 'LE1MEVR.asi'
$marker=Join-Path $asi 'qa-replay-jump.use-steamxr'
$data=Join-Path $ctx.LocalAppData 'MEVR'
$manifest=Join-Path $RunDirectory 'before.json'
if($Action -eq 'Prepare'){
    if(@(Get-QaProcesses).Count){throw 'Another game, launcher or SteamVR session is active.'}
    if(@(Get-ChildItem -LiteralPath $data -Filter '*.request' -ErrorAction SilentlyContinue).Count){throw 'Pending test request exists.'}
    if((Get-FileHash -LiteralPath $binary).Hash -ne $ctx.ExpectedAsiSha256){throw 'Installed ASI changed since preflight.'}
    $paths=@($binary,(Join-Path $asi 'LE1MEVR.ini'),(Join-Path $asi 'MELEVR.ini'),$marker,
        (Join-Path $ctx.SteamRoot 'config\steamvr.vrsettings'),(Join-Path $ctx.SteamRoot 'config\chaperone_info.vrchap'),
        (Join-Path $ctx.SteamVRRoot 'drivers\null\resources\settings\default.vrsettings'))
    Save-QaBackup $RunDirectory $paths @($ctx.ProfileRoot,(Join-Path $ctx.GameRoot 'Game\ME1\BioGame\Config')) $ctx.ExpectedAsiSha256 (Get-QaRuntime)
    if($ctx.RuntimeMode -eq 'LegacyQaMarker'){[IO.File]::WriteAllText($marker,'')}
    Invoke-QaTool 'fake-headset.ps1' @{On=$true;Width=2688;Height=2880;SteamRoot=$ctx.SteamRoot;SteamVRRoot=$ctx.SteamVRRoot;BackupDirectory=(Join-Path $RunDirectory 'fake-backup');AppResolutionScale=100}
}else{
    if(Get-Process MassEffect1,MassEffectLauncher,MEVR-VRLauncher -ErrorAction SilentlyContinue){throw 'Game or launcher still running; close the owned session before restoration.'}
    $m=Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json
    if((Get-FileHash -LiteralPath $binary).Hash -ne $m.binary){throw 'Installed binary changed; refusing to overwrite another task.'}
    if((Get-QaRuntime) -ne $m.runtime){throw 'Global OpenXR runtime changed; review shared-machine ownership before restoring.'}
    if($Action -eq 'Restore'){
        foreach($name in 'vrmonitor','vrserver','vrcompositor','vrdashboard','vrwebhelper','vrstartup'){
            Get-Process -Name $name -ErrorAction SilentlyContinue|Stop-Process -Force -ErrorAction SilentlyContinue
        }
        Start-Sleep -Seconds 2
        Restore-QaBackup $RunDirectory
    }
    $state=@{checked=(Get-Date).ToString('o');mismatches=@(Test-QaBackup $m);verifiedFiles=$m.files.Count;
        runtime=Get-QaRuntime;processes=@(Get-QaProcesses|Select-Object ProcessName,Id);
        requests=@(Get-ChildItem -LiteralPath $data -Filter '*.request' -ErrorAction SilentlyContinue|Select-Object -ExpandProperty Name)}
    $state|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $RunDirectory 'final-state.json')
    if($state.mismatches.Count -or $state.processes.Count -or $state.requests.Count){throw 'Final-state check failed; inspect final-state.json.'}
}
