# Clones (or updates) third-party repos into reference/ for reading. reference/ is gitignored.
# Nothing in reference/ is built or run, and no code is copied from it unless its license allows
# (see research/06-repo-licenses.md).
#
# Deliberately NOT fetched:
#   praydog/UEVR (all rights reserved) and the TF2VR mod (closed), to keep this MIT codebase clean.
#   ME3Tweaks/LegendaryExplorer (~690 MB); use the release build instead.

# git writes progress to stderr; Windows PowerShell 5.1 turns that into errors under 'Stop',
# so rely on $LASTEXITCODE instead.
$ErrorActionPreference = 'Continue'
$root = Join-Path $PSScriptRoot '..\reference'
New-Item -ItemType Directory -Force $root | Out-Null

$repos = @(
    @{ Name = 'Overlord';    Url = 'https://github.com/Aeka0/Overlord.git';       Submodules = $false }  # GPL-3.0: design reference only
    @{ Name = 'REFramework'; Url = 'https://github.com/praydog/REFramework.git';  Submodules = $false }  # MIT: OpenXR/D3D11 code source
    @{ Name = 'LExASIs';     Url = 'https://github.com/ME3Tweaks/LExASIs.git';    Submodules = $true  }  # no license: patterns; pulls LExSDKv2
    @{ Name = 'LEBinkProxy'; Url = 'https://github.com/ME3Tweaks/LEBinkProxy.git'; Submodules = $false }  # no license: the ASI loader
)

foreach ($r in $repos) {
    $dest = Join-Path $root $r.Name
    if (Test-Path (Join-Path $dest '.git')) {
        Write-Output "== Updating $($r.Name)"
        git -C $dest pull --ff-only --depth 1
        if ($r.Submodules) { git -C $dest submodule update --init --recursive --depth 1 }
    } else {
        Write-Output "== Cloning $($r.Name)"
        $cloneArgs = @('-c', 'core.longpaths=true', 'clone', '--depth', '1')
        if ($r.Submodules) { $cloneArgs += @('--recurse-submodules', '--shallow-submodules') }
        git @cloneArgs $r.Url $dest
    }
    if ($LASTEXITCODE -ne 0) { throw "git failed for $($r.Name)" }
    $sha = git -C $dest rev-parse --short HEAD
    Write-Output "   $($r.Name) @ $sha"
}
