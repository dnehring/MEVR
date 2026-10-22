<#
.SYNOPSIS
    Applies known names to the Ghidra project: the mod's natives dump (if present), LExSDKv2's
    RVA table and tools/ghidra/symbols-<game>.csv, in that order (later wins).

.DESCRIPTION
    Rerun it whenever symbols-<game>.csv changes or a new natives dump appears
    (%LOCALAPPDATA%\MEVR\dump\natives-<Game>-<timestamp>.csv, written by the mod when
    MEVR_DUMP_NATIVES=1). Idempotent. Clears the decompile cache, whose output would show old names.

.EXAMPLE
    powershell -File tools\ghidra\seed.ps1
#>
param([ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1')
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Assert-Ghidra
$g = Get-GhidraGame $Game
if (-not (Test-Path $g.Marker)) { throw "No imported project for $Game; run tools\ghidra\setup.ps1 first." }
Assert-ProjectFree $g

$seedDir = Join-Path $GhidraRoot 'seed'
New-Item -ItemType Directory -Force $seedDir | Out-Null
$known = Join-Path $seedDir "symbols-$Game.csv"
& python -I "$PSScriptRoot\collect_symbols.py" $Game $known
if ($LASTEXITCODE -ne 0) { throw 'collect_symbols.py failed' }

$csvs = @()
$ts = Get-PeTimestamp $g.GameExe
$dump = Get-Item (Join-Path $DumpDir "MassEffect-$ts.text.bin") -ErrorAction SilentlyContinue
if ($dump -and $dump.LastWriteTime -gt (Get-Item $g.Marker).LastWriteTime) {
    Write-Warning "The .text dump is newer than the project (the mod replaced it with a cleaner one?). Rebuild: tools\ghidra\setup.ps1 -Force"
}
$natives = Join-Path $DumpDir "natives-$Game-$ts.csv"
if (Test-Path $natives) {
    $csvs += $natives
} else {
    Write-Host "No natives dump ($natives): set MEVR_DUMP_NATIVES=1 and run the game once to name the natives."
}
$csvs += $known

$log = Join-Path $GhidraLogs "seed-$Game.log"
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = Invoke-Headless (@($GhidraRoot, $g.Project, '-process', $g.ProgramName, '-noanalysis',
    '-scriptPath', $GhidraScripts, '-postScript', 'MevrApplyNames.java') + $csvs) $log
$sw.Stop()
Select-String -Path $log -Pattern 'MEVR: ' | ForEach-Object { $_.Line -replace '^.*?MEVR: ', '' -replace '\s+\(GhidraScript\)\s*$', '' }
if ($code -ne 0 -or -not (Select-String -Path $log -Pattern 'MEVR: seeding done' -Quiet)) {
    throw "Seeding failed (exit $code); see $log"
}
Remove-Item -Recurse -Force (Join-Path $GhidraOut $Game) -ErrorAction SilentlyContinue
$elapsed = '{0:hh\:mm\:ss}' -f $sw.Elapsed
Add-Content (Join-Path $GhidraRoot 'timings.txt') "$(Get-Date -Format s) seed $Game $elapsed"
Write-Host "Seeding took $elapsed."
