# SPDX-License-Identifier: MIT
# Shared settings for tools/ghidra/*.ps1. Dot-source it: . "$PSScriptRoot\common.ps1"
#
# Everything these tools write (the spliced exe, the Ghidra project, logs, decompile output) is
# LOCAL research data under %LOCALAPPDATA%\MEVR\ghidra: never commit or share it.

. "$PSScriptRoot\..\common.ps1"

# Ghidra install: $env:GHIDRA_INSTALL_DIR, else the dev machine's copy.
$GhidraInstall = if ($env:GHIDRA_INSTALL_DIR) { $env:GHIDRA_INSTALL_DIR } else { 'F:\downloads\ghidra_public\ghidra_12.1.4_PUBLIC' }
# JDK 21+ for Ghidra 12: $env:MEVR_GHIDRA_JAVA_HOME, else the newest Adoptium JDK installed.
$GhidraJavaHome = $env:MEVR_GHIDRA_JAVA_HOME
if (-not $GhidraJavaHome) {
    $GhidraJavaHome = Get-ChildItem 'C:\Program Files\Eclipse Adoptium' -Directory -Filter 'jdk-*' -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}

$MevrData = Join-Path $env:LOCALAPPDATA 'MEVR'
$DumpDir = Join-Path $MevrData 'dump'
$GhidraRoot = Join-Path $MevrData 'ghidra'          # the Ghidra project lives here
$GhidraOut = Join-Path $GhidraRoot 'out'            # decompile cache
$GhidraLogs = Join-Path $GhidraRoot 'logs'
$GhidraScripts = Join-Path $PSScriptRoot 'scripts'
$ImageBase = [uint64]0x140000000                     # all three LE exes

# Per game: project name 'LE1', program 'MassEffect1.exe' (the spliced copy keeps the exe's name, so
# Ghidra's program name matches it; GhidraMCP's headless mode looks the program up by that name).
function Get-GhidraGame([string]$Game) {
    $n = $Game.Substring(2)
    $exeName = "MassEffect$n.exe"
    $bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
    [pscustomobject]@{
        Game = $Game
        Project = $Game
        ProgramName = $exeName
        GameExe = Join-Path $bin $exeName
        Gpr = Join-Path $GhidraRoot "$Game.gpr"
        Rep = Join-Path $GhidraRoot "$Game.rep"
        Marker = Join-Path $GhidraRoot "$Game.imported.txt"
        ProgramPath = "/$exeName"
    }
}

# The exe's PE timestamp, which names the mod's .text dump (MassEffect-<TS>.text.bin).
function Get-PeTimestamp([string]$Path) {
    $fs = [IO.File]::OpenRead($Path)
    try {
        $br = New-Object IO.BinaryReader $fs
        $fs.Position = 0x3C; $pe = $br.ReadUInt32()
        $fs.Position = $pe + 8; return '{0:X8}' -f $br.ReadUInt32()
    } finally { $fs.Dispose() }
}

function Assert-Ghidra {
    $script:AnalyzeHeadless = Join-Path $GhidraInstall 'support\analyzeHeadless.bat'
    if (-not (Test-Path $AnalyzeHeadless)) { throw "Ghidra not found at $GhidraInstall (set GHIDRA_INSTALL_DIR)" }
    if (-not $GhidraJavaHome -or -not (Test-Path (Join-Path $GhidraJavaHome 'bin\java.exe'))) {
        throw 'No JDK found (set MEVR_GHIDRA_JAVA_HOME)'
    }
    $env:JAVA_HOME = $GhidraJavaHome
    $env:PATH = "$GhidraJavaHome\bin;$env:PATH"
    Set-GhidraUserDirs
    if (-not $env:GHIDRA_HEADLESS_MAXMEM) { $env:GHIDRA_HEADLESS_MAXMEM = '24G' }
    # analyzeHeadless.bat pins 2 GC / 2 JIT threads; later flags win.
    if (-not $env:GHIDRA_HEADLESS_JAVA_OPTIONS) { $env:GHIDRA_HEADLESS_JAVA_OPTIONS = '-XX:ParallelGCThreads=8 -XX:CICompilerCount=4' }
    New-Item -ItemType Directory -Force $GhidraRoot, $GhidraOut, $GhidraLogs | Out-Null
}

# Ghidra's settings and cache dirs default to %APPDATA%\ghidra and %LOCALAPPDATA%\ghidra. A packaged
# app (e.g. the Claude desktop app) sees those through MSIX redirection, which breaks Ghidra's OSGi
# script loader ("The data file must be inside the data dir"). Keep them under our own folder,
# which every process sees the same way.
function Set-GhidraUserDirs {
    $env:XDG_CONFIG_HOME = Join-Path $GhidraRoot 'settings'
    $env:XDG_CACHE_HOME = Join-Path $GhidraRoot 'cache'
    New-Item -ItemType Directory -Force $env:XDG_CONFIG_HOME, $env:XDG_CACHE_HOME | Out-Null
}

# A Ghidra project can be open in only one process. While it holds the project, Ghidra keeps a
# <name>.lock file next to the .gpr and renews it every few seconds.
$ProjectBusy = 'is open in another process (setup, seed, decompile or the MCP server); close that first'
function Assert-ProjectFree($g) {
    foreach ($name in "$($g.Project).lock", "$($g.Project).lock~") {
        $lock = Get-Item (Join-Path $GhidraRoot $name) -ErrorAction SilentlyContinue
        if ($lock -and $lock.LastWriteTime -gt (Get-Date).AddSeconds(-60)) {
            throw "The Ghidra project $($g.Project) $ProjectBusy."
        }
    }
}

# Runs analyzeHeadless with the given arguments; returns its exit code. Output goes to $LogFile.
function Invoke-Headless([string[]]$Arguments, [string]$LogFile) {
    $argLine = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    $p = Start-Process -FilePath $AnalyzeHeadless -ArgumentList $argLine -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $LogFile -RedirectStandardError "$LogFile.err"
    if (Select-String -Path $LogFile -Pattern 'Unable to lock project' -Quiet) {
        throw "The Ghidra project $($Arguments[1]) $ProjectBusy."
    }
    return $p.ExitCode
}

# Address forms: 0xB72D30 / B72D30 (RVA), exe+0xB72D30, 0x140B72D30 (absolute). Returns the RVA
# as a uint64, or $null for anything else (a symbol name).
function ConvertTo-Rva([string]$Text) {
    $t = $Text.Trim()
    if ($t -match '^(?i)(exe\s*\+\s*)?(0x)?([0-9a-f]+)$' -and ($Matches[1] -or $Matches[2] -or $Matches[3].Length -ge 5)) {
        $v = [Convert]::ToUInt64($Matches[3], 16)
        if ($v -ge $ImageBase) { $v -= $ImageBase }
        return $v
    }
    return $null
}
