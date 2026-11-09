# SPDX-License-Identifier: MIT
# Optional local research only. Requires the existing LE1 Ghidra project; never launches the game.
# Decompiled game content stays outside the repository in the private Ghidra cache.
param()
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$output=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'MEVR\ghidra\out\LE1\cafeteria-engine-spike.txt'
$oldLocalAppData=$env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA=[Environment]::GetFolderPath('LocalApplicationData')
    & "$repo\tools\ghidra\decompile.ps1" '0x6F2AC' '0x1728DE' '0x414E18' '0x41C189' | Out-File -LiteralPath $output
    "Private engine research: $output"
}finally{$env:LOCALAPPDATA=$oldLocalAppData}
