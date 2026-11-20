# SPDX-License-Identifier: MIT
# tools/rig/README.md "Automatic placement": the automatic placement against the hand-placed rigs of ME3's Miranda (her suit
# and the underwear body) and Liara, each bone and region centre within 1 cm. Reads the player's own ME3 files; writes nothing.
param(
    [string]$Rig = (Join-Path $PSScriptRoot 'bin\out\mevr-rig.exe'),
    [string]$GameRoot = $(if ($env:MEVR_GAME_ROOT) { $env:MEVR_GAME_ROOT } else { 'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition' })
)
$cooked = Join-Path $GameRoot 'Game\ME3\BioGame\CookedPCConsole'
$plans = Join-Path $PSScriptRoot 'plans'
$cases = @(
    @('BioD_CitHub_Miranda.pcc', 'HMF_ARM_MIRa_MDL', 'le3-miranda.jsonc', 'body'),
    @('BioD_Nor_100CabinConv.pcc', 'HMF_ARM_NKDc_MDL', 'le3-miranda.jsonc', 'underwear'),
    @('BioD_Nor_130RomLiara.pcc', 'ASA_ARM_LIAd_MDL', 'le3-liara.jsonc', 'body')
)
$failed = 0
foreach ($c in $cases) {
    Write-Output "$($c[1]) ($($c[2]) step $($c[3])):"
    & $Rig place (Join-Path $cooked $c[0]) $c[1] --plan (Join-Path $plans $c[2]) --step $c[3] --game LE3
    if ($LASTEXITCODE -ne 0) { $failed++ }
}
if ($failed) { Write-Output "$failed of $($cases.Count) over 1 cm"; exit 1 }
Write-Output "all $($cases.Count) within 1 cm"
