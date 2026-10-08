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
