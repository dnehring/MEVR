<#
.SYNOPSIS
    Serves the local Ghidra project to an MCP client (GhidraMCP 2.0, headless, stdio).

.DESCRIPTION
    Opens the program that tools/ghidra/setup.ps1 imported and seed.ps1 named
    (%LOCALAPPDATA%\MEVR\ghidra, project LE1, program /MassEffect1.exe) with --no-analyze, through
    GhidraMCP's PyGhidra headless mode. Changes made through MCP tools (renames, comments) are
    saved to the project when the server exits.

    A Ghidra project can be open in one process only: while this server runs, decompile.ps1, seed.ps1
    and setup.ps1 fail with "open in another process", and this server fails to start while any of
    them runs. Stop the MCP client's server (e.g. /mcp in Claude Code) before using them.

    Stdout is the MCP stream, so this script writes nothing there itself. Register it with:
      claude mcp add ghidra -s local -- powershell -NoProfile -ExecutionPolicy Bypass -File "<repo>\tools\ghidra\mcp.ps1"

    Needs uv (uvx) with the GhidraMCP wheel's dependencies (PyGhidra, JPype, mcp) in its cache, and
    the GhidraMCP extension installed in Ghidra. LOCAL research tooling only, not shipped.
#>
param(
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE1',
    # The GhidraMCP Python server wheel (LaurieWired's 2.0 release).
    [string]$Wheel = $(if ($env:MEVR_GHIDRA_MCP_WHEEL) { $env:MEVR_GHIDRA_MCP_WHEEL } else { 'F:\downloads\ghidramcp-2.0\x\GhidraMCP-release-2-0\ghidra_mcp-2.0.0-py3-none-any.whl' }),
    [string]$Transport = 'stdio',
    [switch]$Offline   # resolve the wheel's dependencies from uv's cache only
)
$ErrorActionPreference = 'Stop'
function Fail([string]$Message) { [Console]::Error.WriteLine("mcp.ps1: $Message"); exit 1 }

try {
    . "$PSScriptRoot\common.ps1"
    Assert-Ghidra
    $g = Get-GhidraGame $Game
    if (-not (Test-Path $g.Marker)) { Fail "No imported project for $Game; run tools\ghidra\setup.ps1 first." }
    Assert-ProjectFree $g
    $ts = Get-PeTimestamp $g.GameExe
    $binary = Join-Path $GhidraRoot "bin\$Game-$ts\$($g.ProgramName)"
    if (-not (Test-Path $binary)) { Fail "Spliced exe not found: $binary (run tools\ghidra\setup.ps1)" }
    if (-not (Test-Path $Wheel)) { Fail "GhidraMCP wheel not found: $Wheel (set MEVR_GHIDRA_MCP_WHEEL)" }
    $uvx = (Get-Command uvx -ErrorAction SilentlyContinue).Source
    if (-not $uvx) { Fail 'uvx not found on PATH' }
} catch {
    Fail $_.Exception.Message
}

$wheelUrl = 'file:///' + ((Resolve-Path $Wheel).Path -replace '\\', '/')
$arguments = @()
if ($Offline) { $arguments += '--offline' }
$arguments += @('--python', '3.12', '--from', "ghidra-mcp[headless] @ $wheelUrl", 'ghidra-mcp',
    '--headless', '--binary', $binary, '--project', $GhidraRoot, '--project-name', $g.Project,
    '--no-analyze', '--ghidra-install-dir', $GhidraInstall, '--transport', $Transport, '--ready-timeout', '300')
$argLine = ($arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
[Console]::Error.WriteLine("mcp.ps1: serving $binary from project $($g.Project) in $GhidraRoot")
# Start-Process without redirection hands this process's stdin/stdout to uvx unchanged.
$p = Start-Process -FilePath $uvx -ArgumentList $argLine -NoNewWindow -Wait -PassThru
exit $p.ExitCode
