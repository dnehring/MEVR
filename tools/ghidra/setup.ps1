# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Builds the local Ghidra project for an LE exe: splices the mod's decrypted .text into a copy,
    imports it headless with auto-analysis, then seeds names (tools/ghidra/seed.ps1).

.DESCRIPTION
    The exe's .text is encrypted on disk; the mod dumps the decrypted section once per exe build to
    %LOCALAPPDATA%\MEVR\dump (run the game once with the mod). The project goes to
    %LOCALAPPDATA%\MEVR\ghidra\<Game>.gpr, the program inside it is named after the exe
    (MassEffect1.exe). Idempotent: an existing project is kept unless -Force.

    Analysis of LE1 takes a long time (see tools/ghidra/README.md); run this in the background and
    follow logs\setup-<Game>.log. LOCAL research data: never commit or share any of it.

.EXAMPLE
    powershell -File tools\ghidra\setup.ps1
    powershell -File tools\ghidra\setup.ps1 -Force
#>
param(
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1',
    [switch]$Force,     # delete and rebuild the project (and the spliced copy)
    [switch]$NoSeed     # skip the name seeding after the import
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Assert-Ghidra
$g = Get-GhidraGame $Game

if ((Test-Path $g.Marker) -and -not $Force) {
    Write-Host "Project $($g.Gpr) already imported ($((Get-Content $g.Marker) -join '; ')). Use -Force to rebuild."
    if (-not $NoSeed) { & "$PSScriptRoot\seed.ps1" -Game $Game }
    return
}
if ((Test-Path $g.Gpr) -and -not $Force) {
    throw "$($g.Gpr) exists but its import never finished (no $($g.Marker)). Rerun with -Force."
}
Assert-ProjectFree $g

# 1. Splice the decrypted .text into a copy of the exe.
$ts = Get-PeTimestamp $g.GameExe
$dump = Join-Path $DumpDir "MassEffect-$ts.text.bin"
if (-not (Test-Path $dump)) { throw "No .text dump for exe timestamp $ts ($dump). Run the game once with the mod." }
$binDir = Join-Path $GhidraRoot "bin\$Game-$ts"
$spliced = Join-Path $binDir $g.ProgramName
if ($Force -or -not (Test-Path $spliced)) {
    New-Item -ItemType Directory -Force $binDir | Out-Null
    & python -I "$PSScriptRoot\..\splice_text.py" $g.GameExe $dump $spliced
    if ($LASTEXITCODE -ne 0) { throw 'splice_text.py failed' }
    # A dump taken after hooks went in has jmps over some prologues; undo the ones LExSDKv2 knows.
    & python -I "$PSScriptRoot\restore_prologues.py" $Game $spliced
    if ($LASTEXITCODE -ne 0) { throw 'restore_prologues.py failed' }
}

# 2. Import + auto-analysis.
if ($Force) {
    Remove-Item -Recurse -Force $g.Gpr, $g.Rep, $g.Marker -ErrorAction SilentlyContinue
}
$log = Join-Path $GhidraLogs "setup-$Game.log"
$appLog = Join-Path $GhidraLogs "setup-$Game.application.log"
$cpus = [Math]::Max(1, [Environment]::ProcessorCount - 2)
Write-Host "Importing $spliced into $($g.Gpr) with auto-analysis ($cpus threads). Log: $log"
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = Invoke-Headless @($GhidraRoot, $g.Project,
    '-import', $spliced,
    '-processor', 'x86:LE:64:default', '-cspec', 'windows',
    '-scriptPath', $GhidraScripts,
    '-preScript', 'MevrPreAnalysis.java',
    '-max-cpu', "$cpus",
    '-log', $appLog) $log
$sw.Stop()
$elapsed = '{0:hh\:mm\:ss}' -f $sw.Elapsed
if ($code -ne 0 -or -not (Select-String -Path $log -Pattern 'REPORT: Save succeeded|Import succeeded' -Quiet)) {
    throw "analyzeHeadless failed (exit $code) after $elapsed; see $log"
}
"exe timestamp 0x$ts; spliced $spliced; analysis $elapsed; finished $(Get-Date -Format s)" | Set-Content $g.Marker
Add-Content (Join-Path $GhidraRoot 'timings.txt') "$(Get-Date -Format s) setup $Game import+analysis $elapsed"
Write-Host "Import and analysis took $elapsed."

# 3. Names.
if (-not $NoSeed) { & "$PSScriptRoot\seed.ps1" -Game $Game }
