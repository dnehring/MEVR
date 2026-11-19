# SPDX-License-Identifier: MIT
# The eyes inside the body (research/29 "The eyes inside the body"), dot-sourced by tools/fake-headset-session.ps1:
#
#   tools\fake-headset-session.ps1 -Tag eyes -Run tools\eyes-in-body.ps1
#
# The Presidium (John02 slot 89 from the validation saves, its scene ended at once), god mode, then for each head (standing,
# a squat of 15 cm, of 30 cm, a squat of 25 cm looking 60 degrees down: a bend's head) with tracked hands held at the chest
# (a replayed recording: the fake headset has no controllers, so without one arm IK and the body follower never run), the
# body's crouch off and on ([Hands] crouchBody, re-read by the mod within a second): a snapshot of both eyes (`mod.torso`'s
# `result.bodyLiftM`, `crouchGapM`, `liftLimited`) and a skeleton trace (`Root` and the ankles: the body down, the feet on
# the floor). The floor lock reads the live headset's height, which a replay doesn't move (research/58 "Replay limit"), so
# the run sets `[Body] floorLock = 0` (re-read by the host within a second) and the view follows the replayed head, and holds
# the body's scale at 1.05 (`mevr body scale`: the legacy scale without the floor lock would follow the squat); the session
# puts the inis back after. Read each squat's lift against the standing head's (the fixed scale leaves a standing gap). What to read: with crouchBody 0 the squats' `bodyLiftM` 0 and the eye images inside the
# collar or chest; with 1 the lift about the squat (-0.15, -0.30 m), the ankles' height unchanged in the traces
# (`python tools\skeleton-audit.py --last 8`), the eyes over the chest as standing.
Add-Type -Namespace MevrEyes -Name Native -MemberDefinition @'
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string path);
'@
. "$PWD\tools\common.ps1"
$ini = Join-Path (Get-MELEBinDir (Find-MELEInstall).Root 'LE1') 'ASI\LE1MEVR.ini'
function Set-Key([string]$section, [string]$key, [string]$value) {
    [void][MevrEyes.Native]::WritePrivateProfileString($section, $key, $value, $ini)
}
$heads = [ordered]@{
    'stand'      = @('--plan', 'stand:600')
    'squat15'    = @('--plan', 'crouch:600', '--crouch-drop', '0.15')
    'squat30'    = @('--plan', 'crouch:600', '--crouch-drop', '0.30')
    'bendlook60' = @('--plan', 'crouch:600', '--crouch-drop', '0.25', '--pitch', '-60')
}
$recordings = @{}
foreach ($name in $heads.Keys) {
    $file = Join-Path $Session "eyes-$name.mevrrec"
    $pyArgs = @('tools\make-walk-recording.py', $file, '3', '90', '--hands', 'chest') + $heads[$name]
    & python @pyArgs | Out-Null
    $recordings[$name] = $file
}
& tools\conversation-from-save.ps1 -All -Career John02 -Only 89 -EndAfter 3 -Seconds 40 2>&1 |
    Select-String -Pattern '^=== slot|loading|did not' | ForEach-Object { Say "load: $($_.Line)" }
& tools\console.ps1 'mevr convo end' 'god' 'mevr body scale 1.05' | Out-Null
Set-Key 'Body' 'floorLock' '0'
Start-Sleep -Seconds 4
$taken = @()
foreach ($name in $heads.Keys) {
    & tools\replay.ps1 -Path $recordings[$name] -Loop | Out-Null
    foreach ($crouch in @('0', '1')) {
        Set-Key 'Hands' 'crouchBody' $crouch
        Say "=== $name, crouchBody $crouch"
        Start-Sleep -Seconds 3
        Say ((& tools\console.ps1 "mevr camera trace 4 eyes-$name-c$crouch") -join ' ')
        $out = & tools\snapshot.ps1 -Delay 2 -Tag "eyes-$name-c$crouch" 2>&1 | Out-String
        Say ($out.Trim() -replace "`r?`n", ' | ')
        $taken += "eyes-$name-c$crouch"
        Start-Sleep -Seconds 2
    }
    & tools\replay.ps1 -Stop | Out-Null
    Start-Sleep -Seconds 2
}
Set-Key 'Hands' 'crouchBody' '1'
& tools\console.ps1 'mevr body scale auto' | Out-Null
# What mod.torso said in each snapshot.
$snapshots = Join-Path $env:LOCALAPPDATA 'MEVR\snapshots'
foreach ($tag in $taken) {
    $json = Get-ChildItem $snapshots -Filter "*-$tag.json" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $json) {
        Say "$tag`: no snapshot"
        continue
    }
    Copy-Item $json.FullName $Session
    Get-ChildItem $snapshots -Filter "$($json.BaseName.Replace("-$tag", ''))*$tag*.png" -ErrorAction SilentlyContinue | Copy-Item -Destination $Session
    $torso = (Get-Content $json.FullName -Raw | ConvertFrom-Json).mod.torso
    $r = $torso.result
    if ($r) {
        Say ("{0}: crouchBody {1}, lift {2} m, gap {3} m, held by the legs {4}, Shepard's head {5} m from its goal" -f $tag, $torso.crouchBody,
            $r.bodyLiftM, $r.crouchGapM, $r.liftLimited, $r.headFromPivotM)
    } else {
        Say "$tag`: no torso result (arm IK not driving: are the replayed hands tracked?)"
    }
}
Say 'done: eyes in the body (python tools\skeleton-audit.py --last 8; the PNGs and JSONs are in the session folder)'
