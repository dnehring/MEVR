# Builds and assembles a release folder (and optionally a zip) for players (research/33). Local only: it publishes
# and uploads nothing.
#
#   dist\MEVR-<version>\
#     MEVR-Setup.exe        the installer (tools/setup)
#     MEVR-Setup.ini        its settings: the update check's repository (a placeholder until there is one)
#     README.md             docs/release-readme.md
#     LICENSE, NOTICE.md
#     payload\LE1MEVR.asi   the monolithic mod (build-mono: -DMEVR_HOT_RELOAD=OFF)
#     payload\bink2w64.dll  our ASI loader (src/loader)
#     payload\fonts\*       the menu's fonts with their OFL licences
#     payload\menu\*        the in-game menu's documents and the setup's own page
#
#   tools\package-release.ps1 [-Version 0.1.0] [-NoBuild] [-Zip]
#
# LExSDKv2 (compiled into LE1MEVR.asi) has no licence: distributing the build needs its author's permission first
# (docs/release-readme.md, research/33). Third-party texture packs are never bundled.
param(
    [string]$Version = '0.1.0',
    [switch]$NoBuild,
    [switch]$Zip
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDir = Join-Path $repo 'build-mono'

if (-not $NoBuild) {
    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        cmake -S $repo -B $buildDir -G 'Visual Studio 17 2022' -A x64 -DMEVR_HOT_RELOAD=OFF
        if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
    }
    if (-not (Select-String -Path (Join-Path $buildDir 'CMakeCache.txt') -Pattern '^MEVR_HOT_RELOAD:BOOL=OFF' -Quiet)) {
        throw "$buildDir isn't a monolithic build (MEVR_HOT_RELOAD must be OFF)"
    }
    cmake --build $buildDir --config Release --target MEVR-LE1 MEVR-Loader MEVR-Setup
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

$out = Join-Path $buildDir 'Release'
foreach ($f in 'LE1MEVR.asi', 'bink2w64.dll', 'MEVR-Setup.exe') {
    if (-not (Test-Path (Join-Path $out $f))) { throw "Not built: $(Join-Path $out $f)" }
}

$dist = Join-Path $repo "dist\MEVR-$Version"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
$payload = Join-Path $dist 'payload'
New-Item -ItemType Directory -Force $payload | Out-Null

Copy-Item (Join-Path $out 'MEVR-Setup.exe') $dist
Copy-Item (Join-Path $out 'LE1MEVR.asi') $payload
Copy-Item (Join-Path $out 'bink2w64.dll') $payload
Copy-Item -Recurse (Join-Path $repo 'third_party\fonts') (Join-Path $payload 'fonts')
Copy-Item -Recurse (Join-Path $repo 'assets\menu') (Join-Path $payload 'menu')
Copy-Item (Join-Path $repo 'docs\release-readme.md') (Join-Path $dist 'README.md')
foreach ($f in 'LICENSE', 'NOTICE.md') {
    Copy-Item (Join-Path $repo $f) $dist
}
$ini = "; MEVR Setup's settings.`r`n[Update]`r`n; The GitHub repository whose Releases the setup checks (the placeholder turns the check off).`r`nrepo = https://github.com/OWNER/REPO`r`n"
[IO.File]::WriteAllText((Join-Path $dist 'MEVR-Setup.ini'), $ini)

Write-Output "Release folder: $dist"
Get-ChildItem -Recurse -File $dist | ForEach-Object {
    '{0,-40} {1,12:N0}  {2}' -f $_.FullName.Substring($dist.Length + 1), $_.Length, (Get-FileHash $_.FullName).Hash.Substring(0, 16)
}

if ($Zip) {
    $zipPath = "$dist.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath }
    Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zipPath
    Write-Output ("Zip: {0} ({1:N1} MB, SHA-256 {2})" -f $zipPath, ((Get-Item $zipPath).Length / 1MB), (Get-FileHash $zipPath).Hash)
}
