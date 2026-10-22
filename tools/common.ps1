# Shared helpers for tools/*.ps1. Dot-source it: . "$PSScriptRoot\common.ps1"

$MELE_AppId = '1328670'

# Returns @{ Root = <game root>; Manifest = <appmanifest path> } for the Steam install of ME LE.
function Find-MELEInstall {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath -replace '/', '\'
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    $libs = @($steam) + (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' |
        ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' })
    foreach ($lib in $libs | Select-Object -Unique) {
        $manifest = Join-Path $lib "steamapps\appmanifest_$MELE_AppId.acf"
        if (Test-Path $manifest) {
            $dir = (Select-String -Path $manifest -Pattern '"installdir"\s+"([^"]+)"').Matches[0].Groups[1].Value
            return [pscustomobject]@{ Root = Join-Path $lib "steamapps\common\$dir"; Manifest = $manifest }
        }
    }
    throw "Steam app $MELE_AppId not found in any library"
}

# Win64 binaries folder for a game: 'LE1' | 'LE2' | 'LE3'.
function Get-MELEBinDir([string]$GameRoot, [string]$Game) {
    $n = $Game.Substring(2)
    return Join-Path $GameRoot "Game\ME$n\Binaries\Win64"
}

# ---- The test loop (src/plugin/TestLoop.hpp): request files the running game polls once a second ----

$MevrDataDir = Join-Path $env:LOCALAPPDATA 'MEVR'

# Writes %LOCALAPPDATA%\MEVR\<Name> (snapshot.request, record.request, replay.request) as UTF-8
# without a byte order mark, through a temporary file so the game never reads half of it.
function Write-MevrRequest([string]$Name, [string]$Text) {
    New-Item -ItemType Directory -Force -Path $MevrDataDir | Out-Null
    $path = Join-Path $MevrDataDir $Name
    $temp = "$path.writing"
    [System.IO.File]::WriteAllText($temp, $Text, (New-Object System.Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $temp -Destination $path -Force
    return $path
}

# The game's test loop status (%LOCALAPPDATA%\MEVR\testloop.status), or $null.
function Get-MevrTestLoopStatus {
    $path = Join-Path $MevrDataDir 'testloop.status'
    if (-not (Test-Path $path)) { return $null }
    try { return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json } catch { return $null }
}
