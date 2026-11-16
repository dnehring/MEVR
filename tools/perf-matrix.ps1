# SPDX-License-Identifier: MIT
# Runs a matrix of [Performance] settings in the running game (any of the three) under a looping head recording and
# summarises it (research/sequel-render-levers.md "Performance research"): for each round and each combination it writes
# the combination's keys into ASI\<GAME>MEVR.ini (the host re-reads them once a second), waits -Settle seconds, takes a
# frame capture (tools/perf-probe.ps1) and, in the first round, -Snapshots eye snapshots a second apart; then puts every
# key back as it was, copies the game's log and runs tools/perf-matrix.py, which prints one row per combination (the
# median over the rounds, with the spread) from the probe's frames and from the render levers' 10 s lines.
#
#   tools/perf-matrix.ps1 -Game LE2 -Tag fp -Combos 'perEyeViewState=1;umbraQueryWait=1','perEyeViewState=1;umbraQueryWait=0'
#   tools/perf-matrix.ps1 -Game LE3 -Tag nor -Combos 'umbraQueryWait=1','umbraQueryWait=0' -Rounds 3 -Seconds 20
#
# The combinations alternate within a round, so drift in the scene shows as spread, not as a difference. Default
# recording: a full head turn every 10 s (made with tools/make-look-recording.py if missing), so every 10 s window of the
# levers' log is one whole turn. Output: %LOCALAPPDATA%\MEVR\sp-lane\matrix\<stamp>-<tag>\ (entries.json, summary.json,
# summary.txt, each probe's text, the log copy). Check the machine is quiet first (no cl, link, MSBuild, ctest).
param(
    [ValidateSet('LE1', 'LE2', 'LE3')][string]$Game = 'LE2',
    [Parameter(Mandatory = $true)][string[]]$Combos,
    [int]$Rounds = 2,
    [double]$Seconds = 20,
    [double]$Settle = 5,
    [string]$Recording = '',
    [string]$Tag = 'matrix',
    [string]$OutDir = '',
    [int]$Snapshots = 1,
    [int]$QuietWaitSeconds = 45,   # before each capture: wait this long at most for no cl, link, ctest or mevr-tests
    [switch]$NoReplay
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

Add-Type -Namespace MevrMatrixIni -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern uint GetPrivateProfileString(string section, string key, string def,
    System.Text.StringBuilder value, uint size, string path);
'@

$processName = "MassEffect$($Game.Substring(2))"
if (-not (Get-Process $processName -ErrorAction SilentlyContinue)) { throw "$processName is not running" }
$bin = Get-MELEBinDir (Find-MELEInstall).Root $Game
$ini = Join-Path $bin "ASI\${Game}MEVR.ini"
if (-not (Test-Path $ini)) { throw "No $ini" }
$log = Join-Path $bin "Logs\${Game}MEVR.log"
if (-not (Test-Path $log)) { $log = Join-Path $MevrDataDir "${Game}MEVR.log" }
if (-not $OutDir) { $OutDir = Join-Path $MevrDataDir ("sp-lane\matrix\{0:yyyyMMdd-HHmmss}-{1}" -f (Get-Date), $Tag) }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# The combinations: "key=value;key=value", [Performance] unless the key is "Section.key".
$parsed = @()
foreach ($c in $Combos) {
    $pairs = @()
    foreach ($p in ($c -split ';' | Where-Object { $_.Trim() })) {
        $k, $v = $p.Trim() -split '=', 2
        $section = 'Performance'
        if ($k -match '^([^.]+)\.(.+)$') { $section = $Matches[1]; $k = $Matches[2] }
        $pairs += [pscustomobject]@{ section = $section; key = $k; value = $v }
    }
    $parsed += , @($pairs)
}
$absent = [string][char]1
$original = @{}
foreach ($pairs in $parsed) { foreach ($p in $pairs) {
    $id = "$($p.section).$($p.key)"
    if (-not $original.ContainsKey($id)) {
        $b = New-Object System.Text.StringBuilder 256
        [void][MevrMatrixIni.Native]::GetPrivateProfileString($p.section, $p.key, $absent, $b, 256, $ini)
        $original[$id] = $b.ToString()
    }
} }

if (-not $NoReplay) {
    if (-not $Recording) {
        $Recording = Join-Path $MevrDataDir 'recordings\look-10s.mevrrec'
        if (-not (Test-Path $Recording)) { & python "$PSScriptRoot\make-look-recording.py" $Recording 10 | Out-Null }
    }
    & "$PSScriptRoot\replay.ps1" -Path $Recording -Loop | Out-Null
    Start-Sleep -Seconds 3
}
$entries = @()
# A quiet machine: no compiler, linker or test run (idle MSBuild nodes linger for minutes after a build: not counted).
$busy = { (Get-Process cl, link, ctest, mevr-tests -ErrorAction SilentlyContinue | Measure-Object).Count }
try {
    for ($round = 1; $round -le $Rounds; ++$round) {
        for ($i = 0; $i -lt $parsed.Count; ++$i) {
            $pairs = $parsed[$i]
            # Every key any combination names is written: the ones this combination lacks go back to their originals.
            foreach ($id in $original.Keys) {
                $section, $key = $id -split '\.', 2
                $mine = $pairs | Where-Object { $_.section -eq $section -and $_.key -eq $key } | Select-Object -First 1
                $value = if ($mine) { $mine.value } elseif ($original[$id] -eq $absent) { [NullString]::Value } else { $original[$id] }
                [void][MevrMatrixIni.Native]::WritePrivateProfileString($section, $key, $value, $ini)
            }
            Start-Sleep -Seconds $Settle
            $captureTag = "$Tag-c$i-r$round"   # (not $tag: PowerShell names ignore case, it would be $Tag)
            $quietDeadline = (Get-Date).AddSeconds($QuietWaitSeconds)
            $waitStart = Get-Date
            while ((& $busy) -gt 0 -and (Get-Date) -lt $quietDeadline) { Start-Sleep -Seconds 5 }
            $waitedQuiet = [int]((Get-Date) - $waitStart).TotalSeconds
            $compilers = & $busy
            $t0 = Get-Date
            $text = & "$PSScriptRoot\perf-probe.ps1" -Seconds $Seconds -Tag $captureTag 2>&1 | Out-String
            $t1 = Get-Date
            $text | Set-Content -Encoding utf8 (Join-Path $OutDir "$captureTag-probe.txt")
            $probe = ($text -split "`r?`n" | Where-Object { $_ -match '-probe\.json$' } | Select-Object -Last 1)
            $snaps = @()
            if ($round -eq 1 -and $Snapshots -gt 0) {
                $since = Get-Date
                & "$PSScriptRoot\snapshot.ps1" -Count $Snapshots -Interval 1 -Tag $captureTag | Out-Null
                $snaps = @(Get-ChildItem (Join-Path $MevrDataDir 'snapshots') -Filter "*.json" -ErrorAction SilentlyContinue |   # (the sequels name snapshots by time only)
                    Where-Object { $_.LastWriteTime -ge $since } | ForEach-Object { $_.FullName })
            }
            $entries += [pscustomobject]@{
                combo = ($Combos[$i]); index = $i; round = $round; tag = $captureTag; probe = $probe
                start = $t0.ToString('HH:mm:ss'); end = $t1.ToString('HH:mm:ss'); seconds = $Seconds
                compilersAtStart = $compilers; compilersAtEnd = (& $busy); waitedForQuietS = $waitedQuiet; snapshots = $snaps
            }
            Write-Host ("{0} [{1}]: {2} (compilers {3}/{4}, waited {5} s for quiet)" -f $captureTag, $Combos[$i], $probe, $compilers,
                $entries[-1].compilersAtEnd, $waitedQuiet)
        }
    }
} finally {
    foreach ($id in $original.Keys) {
        $section, $key = $id -split '\.', 2
        $value = if ($original[$id] -eq $absent) { [NullString]::Value } else { $original[$id] }
        [void][MevrMatrixIni.Native]::WritePrivateProfileString($section, $key, $value, $ini)
    }
    if (-not $NoReplay) { & "$PSScriptRoot\replay.ps1" -Stop | Out-Null }
    $entries | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 (Join-Path $OutDir 'entries.json')
}
Start-Sleep -Seconds 11   # the levers' last 10 s window of the last capture
Copy-Item -LiteralPath $log -Destination (Join-Path $OutDir "${Game}MEVR.log")
& python "$PSScriptRoot\perf-matrix.py" (Join-Path $OutDir 'entries.json') --log (Join-Path $OutDir "${Game}MEVR.log") --write (Join-Path $OutDir 'summary.json') |
    Tee-Object -FilePath (Join-Path $OutDir 'summary.txt')
Write-Host "matrix: $OutDir"
