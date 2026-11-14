# SPDX-License-Identifier: MIT
# The skeleton audit's run (research/72), dot-sourced by tools/fake-headset-session.ps1:
#
#   tools\fake-headset-session.ps1 -Tag skeleton -Run tools\skeleton-audit.ps1
#   python tools\skeleton-audit.py --last 5
#
# The Presidium (John02 slot 89 from the validation saves, its scene ended at once), god mode, then labelled traces of
# Shepard's skeleton (`mevr camera trace <s> <label>`): standing idle twice (an idle fidget comes now and then), being hit,
# a run forward by a replayed stick and standing after it. Without tracked controllers the holsters never draw a gun and
# arm IK doesn't run: these are the game's own animations, what our body, arms and view must handle. The states this run
# can't reach (a gun drawn, crouching, casts, a conversation's look-at) are traced in the headset: research/72 "To trace".
$audit = [ordered]@{}
function Trace([string]$label, [int]$seconds, [scriptblock]$during) {
    Say "=== $label"
    Say ((& tools\console.ps1 "mevr camera trace $seconds $label") -join ' ')
    if ($during) { & $during }
    Start-Sleep -Seconds ($seconds + 1)
}
$recording = Join-Path $env:TEMP 'mevr-skeleton-audit-run.mevrrec'
& python tools\make-walk-recording.py $recording 3 90 --plan 'stand:2,forward:8,stand:30' --stick 0.9 | Out-Null
& tools\conversation-from-save.ps1 -All -Career John02 -Only 89 -EndAfter 3 -Seconds 40 2>&1 |
    Select-String -Pattern '^=== slot|loading|did not' | ForEach-Object { Say "load: $($_.Line)" }
& tools\console.ps1 'mevr convo end' 'god' | Out-Null
Start-Sleep -Seconds 8
Trace 'idle' 15 $null
Trace 'idle2' 15 $null
Trace 'hits' 12 { for ($i = 0; $i -lt 5; $i++) { Start-Sleep -Milliseconds 1800; & tools\console.ps1 'mevr cover hit 1 0' | Out-Null } }
& tools\replay.ps1 -Path $recording -Inputs | Out-Null
Trace 'run' 14 $null
& tools\replay.ps1 -Stop | Out-Null
Trace 'afterrun' 10 $null
Say 'done: skeleton audit (python tools\skeleton-audit.py --last 5)'
