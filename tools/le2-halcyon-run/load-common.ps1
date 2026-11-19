# SPDX-License-Identifier: MIT
# Pure parsing/acceptance helpers for the existing protected load runner.
function ConvertFrom-LoadState([string]$Text, [string]$Game) {
    $pattern = '^mevr state: world (?<world>[^;]+); pawn (?<pawn>[^;]+); game mode (?<mode>none|[0-9]+)(?:; worldId (?<worldId>0x[0-9a-fA-F]+); pawnId (?<pawnId>0x[0-9a-fA-F]+))?$'
    if ($Text.Trim() -notmatch $pattern) { return $null }
    $m = $Matches.Clone()
    if ($Game -eq 'LE3' -and -not $m.worldId) { return $null }
    [pscustomobject]@{ world = $m.world; pawn = ($m.pawn -replace ' at \([^)]*\)$', ''); mode = $m.mode;
        worldId = $m.worldId; pawnId = $m.pawnId; raw = $Text.Trim() }
}
function ConvertFrom-LoadDescriptor([string]$Text) {
    if ($Text.Trim() -notmatch "^mevr savedesc: career '(?<career>[^']*)' save '(?<name>[^']*)' index (?<index>-?[0-9]+) type (?<type>[0-9]+)$") { return $null }
    [pscustomobject]@{ career = $Matches.career; name = $Matches.name; index = [int]$Matches.index; type = [int]$Matches.type }
}
function Get-LoadDescriptor([string]$Career, [string]$FileName, [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2') {
    # ConsoleRequest reads whitespace-delimited tokens. Never permit command injection or a path as a token.
    if ($Career -notmatch '^[\p{L}\p{N}_-]+$' -or [IO.Path]::GetExtension($FileName) -ine '.pcsav' -or
        [IO.Path]::GetFileName($FileName) -cne $FileName) { throw 'Invalid career token or save filename' }
    $name = [IO.Path]::GetFileNameWithoutExtension($FileName)
    $index = -1; $type = -1
    switch -Regex ($name) {
        '^Save_([0-9]+)$' { $index = [int]$Matches[1]; $type = 0; break }
        '^QuickSave$' { $type = 1; break }
        '^AutoSave$' { $type = 2; break }
        '^ChapterSave$' { $type = 3; break }
        '^LegendSave$' {
            if ($Game -ne 'LE3') { throw 'LegendSave loading is audited for LE3 only' }
            $name = 'LegendSave'
            $type = 5; break
        }
        default { throw 'Only manual, quick, auto, chapter and LE3 Legend saves may be loaded; import/export/NG+ are refused' }
    }
    [pscustomobject]@{ career = $Career; name = $name; index = $index; type = $type }
}
function Test-LoadDescriptor($Expected, $Observed) {
    return $null -ne $Observed -and $Expected.career -ceq $Observed.career -and $Expected.name -ceq $Observed.name -and
        $Expected.index -eq $Observed.index -and $Expected.type -eq $Observed.type
}
function Test-LoadGameplay($State) {
    if (-not $State -or $State.mode -ne '0' -or $State.world -eq 'None' -or $State.pawn -eq 'None') { return $false }
    if ($State.worldId -and ([Convert]::ToUInt64($State.worldId.Substring(2), 16) -eq 0 -or
        [Convert]::ToUInt64($State.pawnId.Substring(2), 16) -eq 0)) { return $false }
    return $true
}
function Test-LoadTransition($Before, $Current, [string]$Game) {
    if (-not $Before -or -not $Current) { return $false }
    if ($Before.worldId -and $Current.worldId) {
        return [Convert]::ToUInt64($Before.worldId.Substring(2), 16) -ne [Convert]::ToUInt64($Current.worldId.Substring(2), 16) -or
            [Convert]::ToUInt64($Before.pawnId.Substring(2), 16) -ne [Convert]::ToUInt64($Current.pawnId.Substring(2), 16)
    }
    # LE2's state has no identities: another world, or the same map loaded again with a new player pawn object (the load of an
    # autosave in the level it was written in names the pawn SFXPawn_PlayerSoldier_1 where it was _0; lane SMOKE, 2026-11-19).
    return $Game -eq 'LE2' -and ($Before.world -ne $Current.world -or $Before.pawn -ne $Current.pawn)
}
function Assert-LoadMode($Session, [string]$Game, [bool]$RendererPreview = $false) {
    if ($RendererPreview) {
        if ($Game -ne 'LE3') { throw 'RendererPreview loading is LE3 only' }
        foreach ($flag in @('desktopDump', 'engineProbe', 'engineRequests', 'rendererPreview')) {
            if (-not $Session.PSObject.Properties[$flag] -or $Session.$flag -isnot [bool] -or
                $Session.$flag -ne ($flag -eq 'rendererPreview')) { throw 'Preview load mode metadata mismatch' }
        }
        return
    }
    if ($Session.PSObject.Properties['rendererPreview'] -and
        ($Session.rendererPreview -isnot [bool] -or $Session.rendererPreview)) { throw 'Preview session requires explicit RendererPreview load flag' }
    if ($Game -eq 'LE3') {
        foreach ($flag in @('desktopDump', 'engineProbe', 'engineRequests')) {
            if (-not $Session.PSObject.Properties[$flag] -or $Session.$flag -isnot [bool] -or -not $Session.$flag) {
                throw "LE3 loading requires explicit true $flag session metadata"
            }
        }
    }
}
function Get-LoadArtifactIdentity([string[]]$Paths) {
    @($Paths | ForEach-Object { [pscustomobject]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_).Hash } })
}
function Test-LoadArtifactsUnchanged($Identity) {
    # Every recorded artifact byte for byte, the mod's settings file included: an appended block during the load
    # refuses it as any other change does. launch.ps1 waits out the mod's first-run appends before a load can start
    # (Wait-RunModSettings; gotcha mod-settings-appended-after-xr-ready).
    if (-not @($Identity).Count) { throw 'Missing runtime artifact identities' }
    return @($Identity | Where-Object { (Get-FileHash -LiteralPath $_.path).Hash -ne $_.sha256 }).Count -eq 0
}
function Assert-LoadProcessSet([object[]]$Processes, $Recorded, [string]$ExpectedPath) {
    if ($Processes.Count -ne 1) { throw 'Loading requires exactly the owned game process, with no other game running' }
    Assert-RunProcess $Processes[0] $Recorded $ExpectedPath
}
function ConvertFrom-LoadReply([string]$Raw, [string]$Command) {
    $lines = @($Raw.Trim() -split '\r?\n')
    if ($lines.Count -lt 2 -or $lines[0] -cne "> $Command" -or @($lines | Select-Object -Skip 1 | Where-Object { $_ -match '^> ' }).Count) {
        throw 'Console reply does not match this single request'
    }
    return (($lines | Select-Object -Skip 1) -join "`n").Trim()
}
