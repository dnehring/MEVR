<#
.SYNOPSIS
    Decompiles LE functions from the local Ghidra project (tools/ghidra/setup.ps1), with their
    callers and callees. Also searches names, and renames or comments a function.

.DESCRIPTION
    Targets: RVAs (0xB72D30, B72D30), exe offsets (exe+0xB72D30), absolute addresses
    (0x140B72D30) or names (exact "ABioWeaponRanged::GetMuzzleLoc", the last part "GetMuzzleLoc",
    or a unique substring). An address inside a function decompiles the whole function.

    Output goes to stdout and to %LOCALAPPDATA%\MEVR\ghidra\out\<Game>\ (0x<RVA>.c per function,
    last.txt per run). A function already in the cache prints without starting Ghidra; -NoCache
    forces a fresh decompile. Renames, comments and seeding clear the cache.

    Ghidra runs headless (-process -noanalysis, -readOnly unless renaming or commenting); each run
    costs Ghidra's startup plus loading the program. The project can be open in one process
    only: don't run this while the MCP server (mcp.ps1), setup or seed holds it.

.EXAMPLE
    tools\ghidra\decompile.ps1 0xB72D30 exe+0xC621E0 ProcessEvent
    tools\ghidra\decompile.ps1 -Search GetMuzzle
    tools\ghidra\decompile.ps1 0xC589F0 -Rename ABioWeaponRanged::TraceShot -Record
    tools\ghidra\decompile.ps1 0xC589F0 -Comment "end = start + aim * GetTraceRange" -Record
#>
param(
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$Target,
    [string]$Search,
    [string]$Rename,
    [string]$Comment,
    [switch]$Record,     # also append the rename/comment to tools/ghidra/symbols-<game>.csv
    [switch]$NoCache,
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1'
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
$g = Get-GhidraGame $Game
$cache = Join-Path $GhidraOut $Game
New-Item -ItemType Directory -Force $cache | Out-Null

function Show([string]$Path) {
    Get-Content $Path | Where-Object { $_ -notmatch '^MEVR-RESULT ' }
}

$mode = 'decompile'
$scriptArgs = @()
if ($Search) {
    $mode = 'search'; $scriptArgs = @($Search)
} elseif ($Rename -or $Comment) {
    if (@($Target).Count -ne 1) { throw '-Rename and -Comment take exactly one target' }
    if ($Rename -and $Comment) { throw 'Use -Rename and -Comment separately' }
    $mode = if ($Rename) { 'rename' } else { 'comment' }
    $scriptArgs = @($Target[0], $(if ($Rename) { $Rename } else { $Comment }))
} else {
    if (-not $Target) { throw 'Give one or more RVAs, exe+offsets or names (or -Search text)' }
    $missing = @()
    foreach ($t in $Target) {
        $rva = ConvertTo-Rva $t
        $file = if ($null -ne $rva) { Join-Path $cache ('0x{0:X}.c' -f $rva) } else { $null }
        if (-not $NoCache -and $file -and (Test-Path $file)) {
            Get-Content $file
            Write-Output "// (cached: $file)"
            Write-Output ''
        } else {
            $missing += $t
        }
    }
    if (-not $missing) { return }
    $scriptArgs = $missing
}

if (-not (Test-Path $g.Marker)) { throw "No imported project for $Game; run tools\ghidra\setup.ps1 first." }
Assert-Ghidra
Assert-ProjectFree $g
$outFile = Join-Path $cache 'last.txt'
Remove-Item $outFile -ErrorAction SilentlyContinue
$log = Join-Path $GhidraLogs "decompile-$Game.log"
$ghidraArgs = @($GhidraRoot, $g.Project, '-process', $g.ProgramName, '-noanalysis')
if ($mode -in 'decompile', 'search') { $ghidraArgs += '-readOnly' }
# The script's arguments go through a file: analyzeHeadless.bat mangles cmd's special characters.
$argFile = Join-Path $cache 'args.txt'
[IO.File]::WriteAllLines($argFile, [string[]](@($outFile, $cache, $mode) + $scriptArgs), (New-Object Text.UTF8Encoding $false))
$ghidraArgs += @('-scriptPath', $GhidraScripts, '-postScript', 'MevrDecompile.java', "@$argFile")
$code = Invoke-Headless $ghidraArgs $log
if (-not (Test-Path $outFile)) { throw "Ghidra produced no output (exit $code); see $log" }
Show $outFile

if ($mode -in 'rename', 'comment') {
    $result = Select-String -Path $outFile -Pattern '^MEVR-RESULT (renamed|commented) (0x[0-9A-F]+) (\S+)' | Select-Object -First 1
    if (-not $result) { return }
    Get-ChildItem $cache -Filter '0x*.c' | Remove-Item   # cached output shows the old names
    if ($Record) {
        $m = $result.Matches[0].Groups
        $csv = Join-Path $PSScriptRoot "symbols-$($Game.ToLower()).csv"
        $note = if ($Comment) { $Comment -replace ',', ';' } else { 'decompile.ps1 -Rename' }
        [IO.File]::AppendAllText($csv, "$($m[2].Value),$($m[3].Value),func,$note`n", (New-Object Text.UTF8Encoding $false))
        Write-Output "// recorded in $csv"
    }
}
