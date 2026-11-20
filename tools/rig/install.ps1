# SPDX-License-Identifier: MIT
<#
Puts the rigged packages tools/rig built (%LOCALAPPDATA%\MEVR\rig\<game>\*.pcc, the player's own game data) where the game
loads them: BioGame\CookedPCConsole plus an entry in BioGame\PCConsoleTOC.bin each (the game finds a package by its TOC;
tools/cockpit/package's `toc` verb, which refuses a TOC whose hash it can't describe). The first install keeps the game's
TOC as %LOCALAPPDATA%\MEVR\rig-install\<game>\PCConsoleTOC.bin.orig; -Uninstall puts it back and deletes our packages.
Development install (tools/rig/README.md "Installing"); players' installs are MEVR Setup's job.

  tools/rig/install.ps1 -Game LE3
  tools/rig/install.ps1 -Game LE3 -Uninstall
#>
param(
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE3',
    [string]$GameRoot = $(if ($env:MEVR_GAME_ROOT) { $env:MEVR_GAME_ROOT } else { 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition' }),
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$folder = @{ LE1 = 'ME1'; LE2 = 'ME2'; LE3 = 'ME3' }[$Game]
$bio = Join-Path $GameRoot "Game\$folder\BioGame"
$cooked = Join-Path $bio 'CookedPCConsole'
$toc = Join-Path $bio 'PCConsoleTOC.bin'
$keep = Join-Path $env:LOCALAPPDATA "MEVR\rig-install\$Game"
$orig = Join-Path $keep 'PCConsoleTOC.bin.orig'
$list = Join-Path $keep 'installed.txt'
New-Item -ItemType Directory -Force $keep | Out-Null
# Only this game's process matters: another game of the trilogy has its own folder and TOC.
$exeName = @{ LE1 = 'MassEffect1'; LE2 = 'MassEffect2'; LE3 = 'MassEffect3' }[$Game]
if (Get-Process -Name $exeName -ErrorAction SilentlyContinue) { throw "$exeName is running: close it first" }

if ($Uninstall) {
    if (Test-Path $list) {
        foreach ($name in Get-Content $list) {
            $p = Join-Path $cooked $name
            if (Test-Path $p) { Remove-Item $p; "removed $p" }
        }
        Remove-Item $list
    }
    if (Test-Path $orig) { Copy-Item $orig $toc -Force; Remove-Item $orig; "TOC put back from $orig" }
    return
}

$packages = @(Get-ChildItem (Join-Path $env:LOCALAPPDATA "MEVR\rig\$Game") -Filter '*.pcc' -ErrorAction SilentlyContinue)
if ($packages.Count -eq 0) { throw "no packages in %LOCALAPPDATA%\MEVR\rig\$Game (run mevr-rig rig <plan>)" }
$tool = Join-Path $PSScriptRoot '..\cockpit\package\bin\out\cockpit-package.exe'
if (-not (Test-Path $tool)) {
    dotnet build (Join-Path $PSScriptRoot '..\cockpit\package') -c Release -o (Split-Path $tool) -nologo -v q | Out-Null
}
if (-not (Test-Path $orig)) { Copy-Item $toc $orig; "kept the game's TOC as $orig" }
# Every install starts from the game's own TOC, so entries never pile up.
$current = Join-Path $keep 'PCConsoleTOC.work.bin'
Copy-Item $orig $current -Force
$names = @()
foreach ($p in $packages) {
    Copy-Item $p.FullName (Join-Path $cooked $p.Name) -Force
    $next = Join-Path $keep 'PCConsoleTOC.next.bin'
    & $tool toc --toc $current --out $next --add (Join-Path $cooked $p.Name) --as "BioGame\CookedPCConsole\$($p.Name)"
    if ($LASTEXITCODE -ne 0) { throw "the TOC entry for $($p.Name) failed" }
    Move-Item $next $current -Force
    $names += $p.Name
}
Copy-Item $current $toc -Force
Remove-Item $current
$names | Set-Content -Encoding utf8 $list
"installed $($names.Count) package(s) into $cooked; undo: tools/rig/install.ps1 -Game $Game -Uninstall"
