# A/B of one [Performance] switch in the running game (LE1, research/22 and /32): sets the key in
# ASI\LE1MEVR.ini to each value in turn (the host re-reads it once a second), lets it settle, takes a
# frame capture (tools/perf-probe.ps1) and prints one summary line per capture; alternates for -Rounds
# rounds so drift in the scene shows; puts the key back as it was at the end.
#
#   tools/perf-ab.ps1 -Key depthPrepass -Values 2,1        the game's pre-pass vs none in the second eye
#   tools/perf-ab.ps1 -Key sharePass -Values 0,1 -Rounds 3
#   tools/perf-ab.ps1 -Key pCoresOnly -Values 0,1 -Seconds 15
#
# Stand still (or freeze the world: tools/console.ps1 pause) for the comparison to mean something.
param(
    [Parameter(Mandatory = $true)][string]$Key,
    [string[]]$Values = @('0', '1'),
    [int]$Rounds = 2,
    [double]$Seconds = 10,
    [double]$Settle = 3
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

Add-Type -Namespace MevrIni -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern uint GetPrivateProfileString(string section, string key, string def,
    System.Text.StringBuilder value, uint size, string path);
'@

$ini = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE1') 'ASI\LE1MEVR.ini'
if (-not (Test-Path $ini)) { throw "No $ini (deploy first: tools/deploy-asi.ps1 -Game LE1)" }
$absent = [string][char]1  # GetPrivateProfileString's default: the key isn't there
$buffer = New-Object System.Text.StringBuilder 256
[void][MevrIni.Native]::GetPrivateProfileString('Performance', $Key, $absent, $buffer, 256, $ini)
$original = $buffer.ToString()

$rows = @()
try {
    for ($round = 1; $round -le $Rounds; ++$round) {
        foreach ($value in $Values) {
            [void][MevrIni.Native]::WritePrivateProfileString('Performance', $Key, $value, $ini)
            Start-Sleep -Seconds $Settle
            $tag = "$Key$value-r$round"
            $report = & "$PSScriptRoot\perf-probe.ps1" -Seconds $Seconds -Tag $tag 2>&1 | Out-String
            $pick = {
                param($pattern)
                $line = ($report -split "`r?`n") | Where-Object { $_ -match $pattern } | Select-Object -First 1
                if ($line) { ($line -split '\s+' | Where-Object { $_ -match '^[0-9.]+$' } | Select-Object -First 2) -join ' / ' } else { '-' }
            }
            $rows += [pscustomobject]@{
                Capture        = $tag
                'Frame ms'     = & $pick '^\s+frame interval'
                'Render busy'  = & $pick 'busy, without the XR waits|busy, whole frame'
                'Game busy'    = & $pick 'game: both eyes \+ UI, busy'
                'Draws'        = & $pick '^\s+whole frame\s+[0-9]'
                'GPU span'     = & $pick 'span: whole frame'
            }
            Write-Host ("{0}: frame {1}, render busy {2}, draws {3}" -f $tag, $rows[-1].'Frame ms', $rows[-1].'Render busy', $rows[-1].Draws)
        }
    }
} finally {
    if ($original -eq $absent) {
        [void][MevrIni.Native]::WritePrivateProfileString('Performance', $Key, $null, $ini)
    } else {
        [void][MevrIni.Native]::WritePrivateProfileString('Performance', $Key, $original, $ini)
    }
}
Write-Host "`n(medians / p90, ms; [Performance] $Key restored)"
$rows | Format-Table -AutoSize
