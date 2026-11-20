# SPDX-License-Identifier: MIT
# tools/rig/README.md "Club bodies": before and after pictures of the clubs' bodies (Clubs.cs) as the game will draw them. Per
# game an underwear body and a dress, each in four columns: the reference (ME3 Liara's romance body, LIAd), the game's own
# mesh, the club copy exported from MEVR_Bodies_<GAME>.pcc as it is (the shape baked in), and the club copy with the game's
# strongest per-dancer variation on top (game/Hair: each Breast*/Glute* bone scaled uniformly about a pivot 5 units above it,
# breasts 1.06, glutes 1.03). A body the clubs don't wear has no copy: its rig is exported with Clubs.cs' growth instead (the
# label says so). Rendered by headless Blender (preview-render.py), sheets by preview-sheet.py, as
# %LOCALAPPDATA%\MEVR\rig\previews\<Prefix>-<GAME>.png and <Prefix>-all.png. The player's game data: local only.
param(
    [string]$Rig = '',
    [string]$Blender = '',
    [string]$Prefix = 'sizes3',
    [string]$GameRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition\Game',
    [hashtable]$Bodies = @{
        LE1 = @('HMF_ARM_NKDb_MDL_MEVR', 'HMF_ARM_CTHc_MDL_MEVR')
        LE2 = @('HMF_ARM_NKDa_MDL_MEVR', 'HMF_ARM_CTHd_MDL_MEVR')
        LE3 = @('HMF_ARM_NKDb_MDL_MEVR', 'HMF_ARM_CTHc_MDL_MEVR')
    },
    [double]$Middle = 0.7,
    [double]$Glute = 0.12,
    [double]$VaryBreast = 1.06,
    [double]$VaryGlute = 1.03
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

function Invoke-Idle([string]$exe, [string[]]$argv) {
    $p = Start-Process -FilePath $exe -ArgumentList $argv -NoNewWindow -PassThru `
        -RedirectStandardOutput "$out\last.out" -RedirectStandardError "$out\last.err"
    $null = $p.Handle  # PowerShell 5.1 keeps ExitCode only with the handle opened
    $p.PriorityClass = 'Idle'
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { Get-Content "$out\last.err" -Tail 20; throw "$exe failed ($($p.ExitCode))" }
}
function Export([string]$pkg, [string]$mesh, [string]$name, [string[]]$extra) {
    $obj = Join-Path $objDir "$name.obj"
    Invoke-Idle $Rig (@('export', "`"$pkg`"", $mesh, "`"$obj`"") + $extra)
    "$name=$obj"
}
function SourceOf([string]$game, [string]$rigName) {
    $plan = Get-Content (Join-Path $here "plans\$($game.ToLower())-females.jsonc") -Raw
    $m = [regex]::Match($plan, '"source": "\{' + $game + '\}/([^"]+)", "mesh": "([^"]+)", "name": "' + [regex]::Escape($rigName) + '"')
    if (-not $m.Success) { throw "no step $rigName in the $game plan" }
    $folder = switch ($game) { 'LE1' { 'ME1' } 'LE2' { 'ME2' } default { 'ME3' } }
    @((Join-Path $GameRoot "$folder\BioGame\$($m.Groups[1].Value -replace '/', '\')"), $m.Groups[2].Value)
}
$vary = @('--uniform-breast', $VaryBreast.ToString($inv), '--uniform-glute', $VaryGlute.ToString($inv))

$ref = Export (Join-Path $rigDir 'LE3\MEVR_Bodies_LE3.pcc') 'ASA_ARM_LIAd_MDL_MEVR' 'ref-LIAd' @()
$sheets = @()
foreach ($game in 'LE1', 'LE2', 'LE3') {
    $pkg = Join-Path $rigDir "$game\MEVR_Bodies_$game.pcc"
    $index = Get-Content (Join-Path $rigDir "$game\MEVR_Bodies_$game.json") -Raw | ConvertFrom-Json
    $parts = @()
    foreach ($mesh in $Bodies[$game]) {
        $entry = $index.meshes | Where-Object { $_.name -eq $mesh } | Select-Object -First 1
        $short = $mesh -replace '_MDL_MEVR$', ''
        $tag = "$game-$short"
        $src = SourceOf $game $mesh
        $set = @($ref)
        $set += Export $src[0] $src[1] "$tag-game" @()
        if ($entry -and $entry.club) {
            $a = [double]$entry.clubBreastA
            $set += Export $pkg $entry.club "$tag-club" @()
            $set += Export $pkg $entry.club "$tag-vary" $vary
            $what = 'the club copy'
        } else {
            $a = if ($entry -and $entry.breastMax) { $Middle * ([double]$entry.breastMax - 1) } else { 0 }
            $grow = @('--breast', (1 + $a).ToString($inv), '--glute', (1 + $Glute).ToString($inv))
            $set += Export $pkg $mesh "$tag-club" $grow
            $set += Export $pkg $mesh "$tag-vary" ($grow + $vary)
            $what = 'no club copy (not worn in a club): the same growth'
        }
        Invoke-Idle $Blender (@('-b', '--factory-startup', '-P', "`"$(Join-Path $here 'preview-render.py')`"", '--', "`"$renders`"") + ($set | ForEach-Object { "`"$_`"" }))
        $part = Join-Path $out "$Prefix-$tag.png"
        $labels = @(
            "ref-LIAd=ME3 Liara, romance body|the cap: this grown by 10%",
            "$tag-game=$game $short|as the game made it",
            ("$tag-club={0}|breasts a {1:F2}, glutes a {2:F2} baked" -f $what, $a, $Glute),
            ("$tag-vary=with the strongest variation|uniform: breasts x{0:F2}, glutes x{1:F2}" -f $VaryBreast, $VaryGlute)
        )
        & python (Join-Path $here 'preview-sheet.py') $renders $part "$game ${short}: the club body" @labels
        $parts += $part
    }
    $sheet = Join-Path $out "$Prefix-$game.png"
    & python (Join-Path $here 'preview-sheet.py') --stack $sheet @parts
    $sheets += $sheet
}
& python (Join-Path $here 'preview-sheet.py') --stack (Join-Path $out "$Prefix-all.png") @sheets
"previews in $out"
