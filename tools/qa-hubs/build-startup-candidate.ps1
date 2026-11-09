# SPDX-License-Identifier: MIT
# Build the current checkout; retain the exact pair, configuration and source hashes for native comparisons.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$OutputDirectory,[string]$BuildDirectory='build',[string]$BaselineDirectory='',[string]$SourceDirectory='')
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if($SourceDirectory){$repo=[IO.Path]::GetFullPath($SourceDirectory)}
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $out){throw 'Use a fresh output directory.'}
if(Get-Process MassEffect1,MassEffectLauncher,MSBuild,cl,link -ErrorAction SilentlyContinue){throw 'Close native tests and other builds first.'}
New-Item -ItemType Directory -Path $out|Out-Null
Start-Transcript -LiteralPath "$out\build.txt"|Out-Null
try{
 $sources=@(Get-ChildItem "$repo\src","$repo\tests" -Recurse -File)
 $hashes=@($sources|ForEach-Object {@{path=[IO.Path]::GetRelativePath($repo,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 $hashes|ConvertTo-Json -Depth 4|Set-Content "$out\source-hashes.json"
 if($BaselineDirectory){
  $baseline=@(Get-Content -LiteralPath "$BaselineDirectory\source-hashes.json" -Raw|ConvertFrom-Json)
  $allowed=@('src\game\SystemSettings.cpp','src\game\SystemSettings.hpp','tests\SystemSettingsTests.cpp')
  $changed=@(foreach($entry in $hashes){$old=@($baseline|Where-Object path -eq $entry.path);if($old.Count -ne 1 -or $old[0].sha256 -ne $entry.sha256){$entry.path}})
  if($baseline.Count -ne $hashes.Count -or @($changed|Where-Object {$_ -notin $allowed}).Count){throw 'The paired comparison permits only SystemSettings and its regression tests to change.'}
  $baselineHost=@(Get-Content -LiteralPath "$BaselineDirectory\build-files.json" -Raw|ConvertFrom-Json|Where-Object name -eq 'LE1MEVR.asi')
  if($baselineHost.Count -ne 1 -or (Get-FileHash -LiteralPath "$BaselineDirectory\LE1MEVR.asi").Hash -ne $baselineHost[0].sha256){throw 'Baseline host hash mismatch.'}
  @{baseline=[IO.Path]::GetFullPath($BaselineDirectory);changedSources=$changed;retainedHost=$baselineHost[0].sha256}|
   ConvertTo-Json -Depth 4|Set-Content "$out\paired-source-check.json"
  # Copy-Item retains source timestamps. A restored candidate can be older than the baseline .obj;
  # force the reviewed source deltas through MSBuild without rebuilding unrelated targets.
  foreach($relative in $changed){[IO.File]::SetLastWriteTimeUtc((Join-Path $repo $relative),[datetime]::UtcNow)}
 }
 cmake --build $BuildDirectory --config Release --target MEVR-LE1 mevr-tests -- /m:4 /nodeReuse:false
 if($LASTEXITCODE){throw 'Build failed.'}
 ctest --test-dir $BuildDirectory -C Release --output-on-failure -R '^(mevr-tests|mevr-system-settings|mevr-motion-blur-guard|mevr-perf-probe)$'
 if($LASTEXITCODE){throw 'Focused tests failed.'}
 foreach($entry in $hashes){if((Get-FileHash -LiteralPath (Join-Path $repo $entry.path)).Hash -ne $entry.sha256){throw "Source changed during build: $($entry.path)"}}
 foreach($name in 'LE1MEVR.asi','LE1MEVR_mod.dll'){Copy-Item -LiteralPath (Join-Path $BuildDirectory "Release\$name") -Destination $out}
 if($BaselineDirectory -and @($changed|Where-Object {$_ -like 'src\*'}).Count -and
    (Get-FileHash -LiteralPath "$out\LE1MEVR_mod.dll").Hash -eq (Get-FileHash -LiteralPath "$BaselineDirectory\LE1MEVR_mod.dll").Hash){
  throw 'Changed production source produced the baseline module; refusing stale-binary deployment.'
 }
 if($BaselineDirectory){Copy-Item -LiteralPath "$BaselineDirectory\LE1MEVR.asi" -Destination "$out\LE1MEVR.asi" -Force}
 Copy-Item -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Destination $out
 Copy-Item -LiteralPath "$repo\src\game\SystemSettings.cpp" -Destination $out
 @('LE1MEVR.asi','LE1MEVR_mod.dll')|ForEach-Object {@{name=$_;sha256=(Get-FileHash -LiteralPath "$out\$_").Hash}}|
  ConvertTo-Json|Set-Content "$out\build-files.json"
}finally{Stop-Transcript|Out-Null}
