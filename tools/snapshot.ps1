# Asks the running game (LE1, Halcyon's renderer) for eye snapshots and waits for them.
#
# Writes %LOCALAPPDATA%\MEVR\snapshot.request; the host polls it once a second, captures what each
# eye shows on the next headset frame and writes, in %LOCALAPPDATA%\MEVR\snapshots:
#   <stamp>-left.png, <stamp>-right.png   what each eye shows (world + HUD panel + reticle + squad menu + menu)
#   <stamp>-left-world.png, ...           the world layer alone (when a quad was drawn over it)
#   <stamp>-hud.png, -squad.png, -menu.png the quad layers on their own
#   <stamp>.json                          head, controllers, aim ray, the mod's arm IK / weapon state
# The JSON is written last, so this script waits for it. Prints the paths it finds.
#
#   tools/snapshot.ps1                         one snapshot now
#   tools/snapshot.ps1 -Count 5 -Interval 0.5  five, half a second apart
#   tools/snapshot.ps1 -Delay 3 -Tag aim       one in three seconds, "-aim" in the file names
param(
    [int]$Count = 1,
    [double]$Interval = 1.0,
    [double]$Delay = 0.0,
    [string]$Tag = '',
    [int]$TimeoutSeconds = 0
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"

$dir = Join-Path $MevrDataDir 'snapshots'
$since = Get-Date
$text = "count=$Count interval=$Interval delay=$Delay"
if ($Tag) { $text += " tag=$Tag" }
$request = Write-MevrRequest 'snapshot.request' $text
Write-Host "Requested: $text ($request)"

if ($TimeoutSeconds -le 0) { $TimeoutSeconds = [int](15 + $Delay + $Count * ($Interval + 3)) }
$deadline = $since.AddSeconds($TimeoutSeconds)
$found = @()
while ((Get-Date) -lt $deadline -and $found.Count -lt $Count) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $dir) {
        $found = @(Get-ChildItem -LiteralPath $dir -Filter '*.json' | Where-Object { $_.LastWriteTime -ge $since } |
            Sort-Object LastWriteTime)
    }
}
if (Test-Path $request) {
    Write-Warning "The game hasn't picked up the request (is it running with the host ASI deployed?)"
}
if ($found.Count -lt $Count) {
    $status = Get-MevrTestLoopStatus
    if ($status -and -not $status.framesRunning) {
        Write-Warning 'No headset frames are running (VR session stopped: headset asleep or not started?)'
    }
    Write-Warning "Got $($found.Count) of $Count snapshots within $TimeoutSeconds s"
}
foreach ($json in $found) {
    $meta = Get-Content -Raw -LiteralPath $json.FullName | ConvertFrom-Json
    Write-Host ''
    Write-Host $json.FullName
    foreach ($file in $meta.files) { Write-Host "  $(Join-Path $dir $file)" }
    foreach ($problem in $meta.errors) { Write-Warning "  $problem" }
}
if ($found.Count -lt $Count) { exit 1 }
