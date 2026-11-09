# SPDX-License-Identifier: MIT
# A live diagnostic intervention in a protected session; never a clean culling comparison.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory,[Parameter(Mandatory)][ValidateSet('0','1','auto')][string]$EarlyFrameWait)
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria\common.ps1')
$RunDirectory=[IO.Path]::GetFullPath($RunDirectory);$ctx=Read-QaContext $RunDirectory
if((Get-Content -LiteralPath (Join-Path $ctx.LocalAppData 'MEVR\qa-cafeteria.lock') -Raw) -ne $RunDirectory){throw 'A different run owns the session.'}
[void](Assert-QaOwnedGame)
$ini=Join-Path $ctx.GameRoot 'Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini'
$before=Get-Content -LiteralPath "$RunDirectory\before.json" -Raw|ConvertFrom-Json
$backup=@($before.files|Where-Object path -eq $ini)
if($backup.Count -ne 1 -or -not $backup[0].exists -or (Get-FileHash -LiteralPath $backup[0].saved).Hash -ne $backup[0].sha256){throw 'Protected settings backup is missing or invalid.'}
Add-Type @'
using System.Runtime.InteropServices;
public static class QaPacingIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
}
'@
if(-not [QaPacingIni]::WritePrivateProfileString('Performance','earlyFrameWait',$EarlyFrameWait,$ini)){throw 'Pacing write failed.'}
@{utc=[datetime]::UtcNow.ToString('o');qpc=[Diagnostics.Stopwatch]::GetTimestamp();earlyFrameWait=$EarlyFrameWait;
 diagnosticOnly=$true;restoration='The owning session restores its full settings backup.'}|ConvertTo-Json -Compress|
 Add-Content -LiteralPath "$RunDirectory\pacing-events.jsonl"
Write-Host "Diagnostic earlyFrameWait=$EarlyFrameWait; this capture is excluded from clean comparisons."
