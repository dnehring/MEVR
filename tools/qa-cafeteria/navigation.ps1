# SPDX-License-Identifier: MIT
# Controller-only navigation helpers. Location is read, never written.
param([Parameter(Mandatory)][string]$RunDirectory)
. "$PSScriptRoot\common.ps1"
$ctx=Read-QaContext $RunDirectory
$repo=$ctx.RepoRoot
$py=$ctx.PythonExe
$ErrorActionPreference='Stop'
$script:PulseNumber=0
$script:NavigationStamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
function Read-Position {
 [void](Assert-QaOwnedGame)
 $s=(Invoke-QaTool 'console.ps1' @{Command=@('mevr loc')}) -join "`n"
 if($s -notmatch 'loc (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) rot (-?\d+) (-?\d+) (-?\d+)') {throw "No live position: $s"}
 $script:LastPawnYaw=[int]$Matches[5]
 return @(1..3|ForEach-Object {[double]::Parse($Matches[$_],[Globalization.CultureInfo]::InvariantCulture)})
}
function Walk-Pulse([double]$Yaw,[double]$Seconds,[double]$Stick=.55,[double]$Pitch=0) {
 $script:PulseNumber++
 $path=Join-Path $RunDirectory ('pulse-'+$script:NavigationStamp+('-{0:D3}.mevrrec' -f $script:PulseNumber))
 $inv=[Globalization.CultureInfo]::InvariantCulture
 & $py "$PSScriptRoot\movement.py" $path --seconds $Seconds.ToString($inv) --yaw $Yaw.ToString($inv) --pitch $Pitch.ToString($inv) --y $Stick.ToString($inv) --lead 2 --tail 2
 if($LASTEXITCODE){throw 'Recording generation failed.'}
 Invoke-QaTool 'replay.ps1' @{Path=$path;Inputs=$true;Fovs=$true}
 . "$repo\tools\common.ps1"
 $deadline=(Get-Date).AddSeconds($Seconds+16)
 do {
  Start-Sleep -Seconds 1
  [void](Assert-QaOwnedGame)
  $status=Get-MevrTestLoopStatus
  if(-not $status){throw 'Replay status disappeared.'}
  if((Get-Date) -gt $deadline){throw 'Movement replay did not finish in time.'}
 }while($status.replaying)
}
function Walk-To([string]$Name,[double]$X,[double]$Y,[double]$Z,[double]$Tolerance=50) {
 for($attempt=0;$attempt -lt 8;$attempt++) {
  $p=Read-Position; $dx=$X-$p[0];$dy=$Y-$p[1];$distance=[Math]::Sqrt($dx*$dx+$dy*$dy)
  if($distance -le $Tolerance -and [Math]::Abs($p[2]-$Z) -lt 65) {
   $entry=@{name=$Name;position=$p;target=@($X,$Y,$Z);time=(Get-Date).ToString('o');arrived=$true}
   $entry|ConvertTo-Json -Compress|Add-Content -LiteralPath "$RunDirectory\waypoints.jsonl"
   Write-Host "ARRIVED $Name at $($p -join ', ')";return
  }
  if($distance -le $Tolerance){throw "Wrong deck at $Name; position $p"}
  # Replays are relative to the saved pawn's facing. Preserve Normandy's -16164 default, support other hubs.
  $startYaw=if($null -ne $ctx.StartingYaw){[double]$ctx.StartingYaw}else{-16164.0}
  $yaw=([Math]::Atan2($dy,$dx)*180/[Math]::PI-$startYaw*360/65536+720)%360
  $seconds=($distance-12)/85.0
  if($seconds -lt 0.6){$seconds=0.6}
  if($seconds -gt 8.0){$seconds=8.0}
  Walk-Pulse $yaw $seconds
  $next=Read-Position
  $progress=[Math]::Sqrt([Math]::Pow($next[0]-$p[0],2)+[Math]::Pow($next[1]-$p[1],2))
  if($progress -lt 12){throw "Blocked before $Name at $next. No teleport attempted."}
 }
 throw "Waypoint $Name not reached."
}
function Look-At([double]$Yaw,[double]$Pitch=-10) {
 $path=Join-Path $RunDirectory 'look.mevrrec'
 $inv=[Globalization.CultureInfo]::InvariantCulture
 & $py "$PSScriptRoot\movement.py" $path --seconds 30 --yaw $Yaw.ToString($inv) --pitch $Pitch.ToString($inv) --tail 0
 if($LASTEXITCODE){throw 'Look recording generation failed.'}
 Invoke-QaTool 'replay.ps1' @{Path=$path;Loop=$true;Fovs=$true}
}
