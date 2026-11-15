# SPDX-License-Identifier: MIT
# One protected stretch: complete verified backups precede the loader and deployment.
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [switch]$DesktopDump,
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag = 'halcyon',
    [string]$BuildDir,
    [switch]$Diagnostics
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\session-common.ps1"
Assert-RunMode $Game ([bool]$DesktopDump)
if ($DesktopDump -and $Diagnostics) { throw '-Diagnostics is only for the LE2 renderer' }
. "$PSScriptRoot\..\common.ps1"
$W = (Resolve-Path "$PSScriptRoot\..\..").Path
if (-not $BuildDir) { $BuildDir = Join-Path $W 'build-trilogy' }
if ($DesktopDump -and -not (Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^MEVR_HOT_RELOAD:BOOL=ON' -Quiet)) {
    throw 'DesktopDump requires a development hot-reload build; players builds do not have the dump-only gate'
}
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir "Release\${Game}MEVR.asi"))) { throw 'Selected game host has not been built' }
if ($DesktopDump) { Assert-RunDumpHost (Join-Path $BuildDir "Release\${Game}MEVR.asi") }
$names = Get-RunNames $Game
$install = (Find-MELEInstall).Root
$G = Join-Path $install 'Game'
$b = Get-MELEBinDir $install $Game
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'
$current = $names.current
if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) { throw 'A game or launcher runs: close only your owned session first' }
$open = $null
if (Test-Path -LiteralPath $current) {
    $open = Get-Content -LiteralPath $current -Raw | ConvertFrom-Json
    Assert-RunSession $open $Holder $Game ([bool]$DesktopDump)
}
# A second game stretch by the same holder must not create overlapping profile backups.
$other = Get-RunNames $(if ($Game -eq 'LE2') { 'LE3' } else { 'LE2' })
if (Test-Path -LiteralPath $other.current) { throw 'Another game stretch remains open; restore it first' }
& powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\run-lock.ps1" -Take -Holder $Holder -Note "$Game runs ($Tag), desktopDump=$([bool]$DesktopDump)"
if ($LASTEXITCODE -ne 0) { throw 'The run lock is not ours' }
if (-not $open) {
    if (Test-Path -LiteralPath "$b\bink2w64_original.dll") { throw 'An existing loader has no open stretch: not ours to replace' }
    $proxy = Get-Item -LiteralPath "$G\ME1\Binaries\Win64\bink2w64.dll"
    if ($proxy.VersionInfo.ProductName -notmatch 'LEBinkProxy') { throw "ME1's loader is not LEBinkProxy" }
    if ((Get-Item -LiteralPath "$b\bink2w64.dll").VersionInfo.ProductName -ne 'Bink and Smacker') { throw 'Selected game does not have its stock Bink DLL' }
    $sessions = Join-Path $env:LOCALAPPDATA 'MEVR\sessions'
    $S = Resolve-RunChild $sessions ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + "-$Game-$Tag")
    if (Test-Path -LiteralPath $S) { throw 'Session directory already exists' }
    New-Item -ItemType Directory "$S\backup" | Out-Null
    foreach ($r in @(@($docs, 'profile'), @("$G\$($names.folder)\BioGame\Config", "$($names.folder)Config"), @($b, "$($names.folder)Win64"))) {
        Backup-RunRoot $r[0] "$S\backup\$($r[1])" "$S\before-$($r[1]).json"
    }
    New-Item -ItemType Directory "$S\backup\ME1ASI" | Out-Null
    Copy-Item "$G\ME1\Binaries\Win64\ASI\*.ini" "$S\backup\ME1ASI\" -ErrorAction SilentlyContinue
    $open = [pscustomobject]@{ holder = $Holder; game = $Game; desktopDump = [bool]$DesktopDump; session = $S;
        since = (Get-Date -Format 's'); install = $install; profile = $docs }
    Assert-RunLock $Holder
    if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) { throw 'A game started during backup; deployment refused' }
    Write-RunSession $open $current
    "Verified backups in $S"
    Rename-Item -LiteralPath "$b\bink2w64.dll" -NewName 'bink2w64_original.dll'
    Copy-Item -LiteralPath $proxy.FullName -Destination "$b\bink2w64.dll"
} else {
    $open = Read-RunSession $Holder $Game ([bool]$DesktopDump)
    if (($open.install -and $open.install -ne $install) -or ($open.profile -and $open.profile -ne $docs)) { throw 'Install/profile changed since backup' }
    "The stretch in $($open.session) is open: redeploying only"
}
Assert-RunLock $Holder
if (Get-Process MassEffect1, MassEffect2, MassEffect3, MassEffectLauncher -ErrorAction SilentlyContinue) { throw 'A game started before deployment; deployment refused' }
& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\deploy-asi.ps1" -Game $Game -BuildDir $BuildDir -GameRoot $install
if ($LASTEXITCODE -ne 0) { throw 'Deploy failed; restore the open stretch before releasing its lock' }
if ($DesktopDump) {
    [IO.File]::WriteAllText("$b\ASI\${Game}MEVR.ini", "[Debug]`r`ndumpOnly = 1`r`n")
    'Desktop dump-only prepared: no fake headset or VR settings were invoked'
    exit 0
}
& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\vr-game-settings.ps1" -Game $Game | Select-String 'Borderless|Fullscreen|Applied'
if ($LASTEXITCODE -ne 0) { throw 'VR game settings failed' }
$steamvr = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json'
[IO.File]::WriteAllText("$b\ASI\${Game}MEVR.ini", "[Debug]`r`nopenxrRuntime = $steamvr`r`n")
$ini = Join-Path $b 'MELE2VR.ini'
if ($Diagnostics -and (Test-Path -LiteralPath $ini)) {
    $t = [IO.File]::ReadAllText($ini) -replace '(?m)^Diagnostics=0', 'Diagnostics=1'
    [IO.File]::WriteAllText($ini, $t)
}
& powershell -NoProfile -ExecutionPolicy Bypass -File "$W\tools\fake-headset.ps1" -On | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { throw 'Fake-headset setup failed' }
