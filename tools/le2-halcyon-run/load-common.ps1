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
function Get-LoadDescriptor([string]$Career, [string]$FileName) {
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
        default { throw 'Only normal manual, quick, auto and chapter saves may be loaded; import/export/NG+ are refused' }
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
    return $Game -eq 'LE2' -and $Before.world -ne $Current.world
}
function Assert-LoadMode($Session, [string]$Game) {
    if ($Game -eq 'LE3') {
        foreach ($flag in @('desktopDump', 'engineProbe', 'engineRequests')) {
            if (-not $Session.PSObject.Properties[$flag] -or $Session.$flag -isnot [bool] -or -not $Session.$flag) {
                throw "LE3 loading requires explicit true $flag session metadata"
            }
        }
    }
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
