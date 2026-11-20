# SPDX-License-Identifier: MIT
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

# ---- Is the game's world drawn? (tools/game-watch.ps1; the host's "world" block in testloop.status) ----
# One reading of a running Mass Effect: what state it is in and how long it has been so. The host writes "world" once a
# second from the game thread (development builds and [Debug] requests = 1): unixMs (when), msSincePresent, msSinceScene
# (the last Present that had a scene render in it). A loading screen presents frames with no scene; a hard map load
# blocks the game thread (the file stops); a load that never ends stays one of the two (the 2026-11-19/20 ME3 hangs).
#   State  'no-game'   no MassEffect process
#          'starting'  a game process whose host hasn't written a "world" yet (a launch, the logos, an older host)
#          'drawn'     a scene was rendered within the last 2 s
#          'no-world'  frames present but no scene (a loading screen, a movie, a load that may be stuck): see Seconds
#          'blocked'   the game thread hasn't written the status for 5 s (a hard load, a hang, a debugger)
#          'no-frames' the game thread ticks but nothing was presented for 5 s (the render thread stopped)
#   Seconds  how long the state has lasted, as far as the status can tell
function Get-MevrWorldVerdict {
    $proc = @(Get-Process -Name 'MassEffect1', 'MassEffect2', 'MassEffect3' -ErrorAction SilentlyContinue) |
        Sort-Object StartTime -Descending | Select-Object -First 1
    $v = [ordered]@{ State = 'no-game'; Seconds = 0.0; Game = ''; Pid = 0; Detail = 'no Mass Effect game is running';
        ProcessSeconds = 0.0; MsSinceScene = $null; MsSincePresent = $null; StatusAgeS = $null }
    if ($null -eq $proc) { return [pscustomobject]$v }
    $v.Pid = $proc.Id
    $v.Game = 'LE' + $proc.ProcessName.Substring(10)
    $v.ProcessSeconds = ((Get-Date) - $proc.StartTime).TotalSeconds
    $status = Get-MevrTestLoopStatus
    $world = if ($null -ne $status -and $status.PSObject.Properties['world']) { $status.world } else { $null }
    if ($null -eq $world -or $world.pid -ne $proc.Id) {
        $v.State = 'starting'
        $v.Seconds = $v.ProcessSeconds
        $v.Detail = "$($v.Game) (pid $($proc.Id)) up $([int]$v.ProcessSeconds) s; its host hasn't reported the world yet" +
            $(if ($null -eq $world) { ' (no "world" in testloop.status: a launch, an older host, or a players'' build without [Debug] requests = 1)' } else { '' })
        return [pscustomobject]$v
    }
    $nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    $ageS = [Math]::Max(0.0, ($nowMs - [double]$world.unixMs) / 1000.0)
    $v.StatusAgeS = [Math]::Round($ageS, 1)
    $v.MsSinceScene = $world.msSinceScene
    $v.MsSincePresent = $world.msSincePresent
    if ($ageS -gt 5) {
        $v.State = 'blocked'
        $v.Seconds = $ageS
        $v.Detail = "$($v.Game): the game thread hasn't ticked for $([int]$ageS) s (a hard map load, or hung)"
    } elseif ($world.msSincePresent -ge 0 -and $world.msSincePresent -gt 5000) {
        $v.State = 'no-frames'
        $v.Seconds = $world.msSincePresent / 1000.0
        $v.Detail = "$($v.Game): no frame presented for $([int]$v.Seconds) s while the game thread ticks (the render thread stopped?)"
    } elseif (-not $world.sceneCounted -or $world.msSinceScene -lt 0) {
        $v.State = 'drawn'
        $v.Detail = "$($v.Game): presenting; this host doesn't count scene renders (no world-drawn clock)"
    } elseif ($world.msSinceScene -gt 2000) {
        $v.State = 'no-world'
        $v.Seconds = ($world.msSinceScene / 1000.0) + $ageS
        $v.Detail = "$($v.Game): frames present but no scene drawn for $([int]$v.Seconds) s (a loading screen or a movie)"
    } else {
        $v.State = 'drawn'
        $v.Detail = "$($v.Game): the world is drawn (a scene $($world.msSinceScene) ms ago)"
    }
    return [pscustomobject]$v
}
