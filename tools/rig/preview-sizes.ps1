# SPDX-License-Identifier: MIT
# tools/rig/README.md "Sizes": before and after pictures of the dancers' size boost. Per game a bare body and a dress (a skirt
# fitted under the glutes, Fit.cs) from MEVR_Bodies_<GAME>.pcc, each in three states: as the game made it (the game's own
# mesh: for a dress without the fit), a middle dancer (breasts a = 0.7 (breastMax - 1), glutes a = 0.12) and the upper end
# (breastMax, glutes 0.16), beside the reference (ME3 Liara's romance body, LIAd; the cap is it grown by 10%). Exported by
# `mevr-rig export` (the growth of Sizes.cs), rendered by headless Blender (preview-render.py), put on contact sheets
# (preview-sheet.py) as %LOCALAPPDATA%\MEVR\rig\previews\<Prefix>-<GAME>.png and <Prefix>-all.png. Everything here is the
# player's game data: local only, never committed.
param(
    [string]$Rig = '',
    [string]$Blender = '',
    [string]$Prefix = 'sizes2',
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game',
    [hashtable]$Bodies = @{
        LE1 = @('HMF_ARM_NKDa_MDL_MEVR', 'HMF_ARM_CTHc_MDL_MEVR')
        LE2 = @('HMF_ARM_NKDa_MDL_MEVR', 'HMF_ARM_CTHd_MDL_MEVR')
        LE3 = @('HMF_ARM_NKDa_MDL_MEVR', 'HMF_ARM_CTHc_MDL_MEVR')
    },
    [double]$Middle = 0.7,
    [double]$GluteMiddle = 0.12,
    [double]$GluteUpper = 0.16,
    [string]$BreastShape = '',
    [string]$GluteShape = ''
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Rig) { $Rig = Join-Path $here 'bin\out\mevr-rig.exe' }
if (-not $Blender) {
    $Blender = Get-ChildItem 'C:\Program Files\Blender Foundation' -Recurse -Filter blender.exe -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Blender) { throw 'No Blender found: pass -Blender' }
$rigDir = Join-Path $env:LOCALAPPDATA 'MEVR\rig'
$out = Join-Path $rigDir 'previews'
$objDir = Join-Path $out "obj-$Prefix"
$renders = Join-Path $out "renders-$Prefix"
New-Item -ItemType Directory -Force $objDir, $renders | Out-Null
$inv = [Globalization.CultureInfo]::InvariantCulture
$shapeArgs = @()
if ($BreastShape) { $shapeArgs += @('--breast-shape', $BreastShape) }
if ($GluteShape) { $shapeArgs += @('--glute-shape', $GluteShape) }

function Invoke-Idle([string]$exe, [string[]]$argv) {
    $p = Start-Process -FilePath $exe -ArgumentList $argv -NoNewWindow -PassThru `
        -RedirectStandardOutput "$out\last.out" -RedirectStandardError "$out\last.err"
    $null = $p.Handle  # PowerShell 5.1 keeps ExitCode only with the handle opened
    $p.PriorityClass = 'Idle'
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { Get-Content "$out\last.err" -Tail 20; throw "$exe failed ($($p.ExitCode))" }
}
# One OBJ: from a package (ours, or the game's for "as the game made it"), grown by a (breasts) and g (glutes).
function Export([string]$pkg, [string]$mesh, [string]$name, [double]$a, [double]$g) {
    $obj = Join-Path $objDir "$name.obj"
    Invoke-Idle $Rig (@('export', "`"$pkg`"", $mesh, "`"$obj`"", '--breast', (1 + $a).ToString($inv), '--glute', (1 + $g).ToString($inv)) + $shapeArgs)
    "$name=$obj"
}
# The game's own mesh a rig was built from: its plan step's source and mesh.
function SourceOf([string]$game, [string]$rigName) {
    $plan = Get-Content (Join-Path $here "plans\$($game.ToLower())-females.jsonc") -Raw
    $m = [regex]::Match($plan, '"source": "\{' + $game + '\}/([^"]+)", "mesh": "([^"]+)", "name": "' + [regex]::Escape($rigName) + '"')
    if (-not $m.Success) { throw "no step $rigName in the $game plan" }
    $folder = switch ($game) { 'LE1' { 'ME1' } 'LE2' { 'ME2' } default { 'ME3' } }
    @((Join-Path $GameRoot "$folder\BioGame\$($m.Groups[1].Value -replace '/', '\')"), $m.Groups[2].Value)
}

$ref = Export (Join-Path $rigDir 'LE3\MEVR_Bodies_LE3.pcc') 'ASA_ARM_LIAd_MDL_MEVR' 'ref-LIAd' 0 0
$sheets = @()
foreach ($game in 'LE1', 'LE2', 'LE3') {
    $pkg = Join-Path $rigDir "$game\MEVR_Bodies_$game.pcc"
    $index = Get-Content (Join-Path $rigDir "$game\MEVR_Bodies_$game.json") -Raw | ConvertFrom-Json
    $parts = @()
    foreach ($mesh in $Bodies[$game]) {
        $entry = $index.meshes | Where-Object { $_.name -eq $mesh } | Select-Object -First 1
        $max = if ($entry -and $entry.breastMax) { [double]$entry.breastMax - 1 } else { 0.25 }
        $mid = $Middle * $max
        $short = $mesh -replace '_MDL_MEVR$', ''
        $tag = "$game-$short"
        $src = SourceOf $game $mesh
        $set = @($ref)
        $set += Export $src[0] $src[1] "$tag-before" 0 0
        $set += Export $pkg $mesh "$tag-middle" $mid $GluteMiddle
        $set += Export $pkg $mesh "$tag-upper" $max $GluteUpper
        Invoke-Idle $Blender (@('-b', '--factory-startup', '-P', "`"$(Join-Path $here 'preview-render.py')`"", '--', "`"$renders`"") + ($set | ForEach-Object { "`"$_`"" }))
        $part = Join-Path $out "$Prefix-$tag.png"
        $labels = @(
            "ref-LIAd=ME3 Liara, romance body|the cap: this grown by 10%",
            "$tag-before=$game $short|as the game made it",
            ("$tag-middle=a middle dancer|breasts a {0:F2}, glutes a {1:F2}" -f $mid, $GluteMiddle),
            ("$tag-upper=the upper end|breasts a {0:F2} (breastMax), glutes a {1:F2}" -f $max, $GluteUpper)
        )
        & python (Join-Path $here 'preview-sheet.py') $renders $part "$game ${short}: before and after" @labels
        $parts += $part
    }
    $sheet = Join-Path $out "$Prefix-$game.png"
    & python (Join-Path $here 'preview-sheet.py') --stack $sheet @parts
    $sheets += $sheet
}
& python (Join-Path $here 'preview-sheet.py') --stack (Join-Path $out "$Prefix-all.png") @sheets
"previews in $out"
