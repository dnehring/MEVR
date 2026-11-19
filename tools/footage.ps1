# SPDX-License-Identifier: MIT
<#
Footage of the running game for judging motion by eye (tools/motion/README.md "Footage"): our motion slowed (`mevr anim
slowmo`: the generated clip, the hair simulation and the driver's walk; the world's TimeDilation didn't reach a conversation
character's pose updates, 2026-11-19), the test loop's snapshots taken at a steady interval, the speed put back, and the
frames cropped into an animated GIF at the real rate. Slowed by -Dilation, a snapshot every -Interval seconds is -Interval *
-Dilation of motion time: 0.1 and 0.333 give 30 frames per second. The physics sees the same steps, only spread out; the
game's own animation (the face, the conversation) is not slowed.

  tools/footage.ps1 -Frames 60 -Out miranda-walk.gif -Crop 0.35,0.15,0.30,0.70
  -Crop: left, top, width, height as fractions of the eye image (default the middle third)
  -Eye left|right, -Dilation 0.1, -Interval 0.333, -Scale 0.5 (the GIF's size against the crop)

Needs a development build (the request files) and a Python with Pillow ($env:MEVR_MOTION_PYTHON, else
I:\miniconda3\envs\py310\python.exe). The GIF and its frames go to %LOCALAPPDATA%\MEVR\footage\<stamp>.
#>
param(
    [int]$Frames = 60,
    [double]$Interval = 0.333,
    [double]$Dilation = 0.1,
    [string]$Crop = '0.333,0.1,0.333,0.8',
    [ValidateSet('left', 'right')][string]$Eye = 'left',
    [double]$Scale = 0.5,
    [string]$Out = '',
    [string]$SlowCommand = 'mevr anim slowmo {0}'
)
$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$python = if ($env:MEVR_MOTION_PYTHON) { $env:MEVR_MOTION_PYTHON } else { 'I:\miniconda3\envs\py310\python.exe' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$folder = Join-Path $env:LOCALAPPDATA "MEVR\footage\$stamp"
New-Item -ItemType Directory -Force $folder | Out-Null
$snapshots = Join-Path $env:LOCALAPPDATA 'MEVR\snapshots'
$started = Get-Date

& (Join-Path $tools 'console.ps1') ($SlowCommand -f $Dilation) | Out-Null
try {
    & (Join-Path $tools 'snapshot.ps1') -Count $Frames -Interval $Interval -Tag "footage-$stamp" | Out-Null
} finally {
    & (Join-Path $tools 'console.ps1') ($SlowCommand -f 1) | Out-Null
}
$shots = @(Get-ChildItem $snapshots -Filter "*-$Eye.png" | Where-Object { $_.LastWriteTime -ge $started -and $_.Name -match ("^\d{4}-\d\d-\d\d_\d\d-\d\d-\d\d-\d{3}-" + $Eye + "\.png$") } | Sort-Object Name)
if ($shots.Count -lt 2) { throw "only $($shots.Count) snapshots arrived (is the game running a VR session with a development build?)" }
$list = Join-Path $folder 'frames.txt'
$shots.FullName | Set-Content -Encoding utf8 $list
if (-not $Out) { $Out = Join-Path $folder 'footage.gif' } elseif (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $folder $Out }
$script = @'
import sys, os, re, datetime
from PIL import Image
frames_file, out, crop, scale, dilation = sys.argv[1], sys.argv[2], [float(v) for v in sys.argv[3].split(',')], float(sys.argv[4]), float(sys.argv[5])
paths = [p.strip().lstrip(chr(0xFEFF)) for p in open(frames_file, encoding='utf-8') if p.strip()]
def taken(p):
    m = re.match(r'(\d{4})-(\d\d)-(\d\d)_(\d\d)-(\d\d)-(\d\d)-(\d{3})', os.path.basename(p))
    y, mo, d, h, mi, s, ms = (int(v) for v in m.groups())
    return datetime.datetime(y, mo, d, h, mi, s, ms * 1000).timestamp()
times = [taken(p) for p in paths]
images = []
for p in paths:
    im = Image.open(p).convert('RGB')
    w, h = im.size
    box = (int(crop[0] * w), int(crop[1] * h), int((crop[0] + crop[2]) * w), int((crop[1] + crop[3]) * h))
    im = im.crop(box)
    im = im.resize((max(1, int(im.width * scale)), max(1, int(im.height * scale))), Image.LANCZOS)
    images.append(im.quantize(colors=255, method=Image.Quantize.MEDIANCUT))
# Each frame lasts the motion time to the next one: the real interval it was taken at, times the slow motion.
gaps = [max(0.001, b - a) for a, b in zip(times, times[1:])]
mean = sum(gaps) / len(gaps)
durations = [int(round(g * dilation * 1000)) for g in gaps] + [int(round(mean * dilation * 1000))]
durations = [max(20, d) for d in durations]
images[0].save(out, save_all=True, append_images=images[1:], duration=durations, loop=0, optimize=False)
print(f"{len(images)} frames, {images[0].width}x{images[0].height}, one every {mean:.3f} s taken = {1 / (mean * dilation):.1f} fps of motion -> {out}")
'@
$py = Join-Path $folder 'assemble.py'
Set-Content -Encoding utf8 -Path $py -Value $script
& $python $py $list $Out $Crop $Scale $Dilation
if ($LASTEXITCODE -ne 0) { throw 'assembling the GIF failed' }
"frames: $($shots.Count) ($($shots[0].Name) .. $($shots[-1].Name)); slowed x$Dilation, every $Interval s"
