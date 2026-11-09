# SPDX-License-Identifier: MIT
# Re-run the existing analyzers on a finished session; no game/runtime access.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$RunDirectory, [string]$PythonExe='python')
$ErrorActionPreference='Stop'
$run=[IO.Path]::GetFullPath($RunDirectory)
if(-not (Test-Path -LiteralPath "$run\capture-window.json")){throw 'A completed capture directory is required.'}
$kit=Join-Path (Split-Path $PSScriptRoot -Parent) 'qa-cafeteria'
foreach($name in @('analyze','check-tick-trace','investigate')){
 $output=& $PythonExe "$kit\$name.py" $run
 $result=$LASTEXITCODE
 $output|Set-Content -LiteralPath "$run\$name-console.jsonl"
 if($result){throw "$name failed; see $run\$name-console.jsonl"}
}
foreach($probe in @(Get-Content -LiteralPath "$run\analysis.json" -Raw|ConvertFrom-Json)){
 [pscustomobject]@{
  probe=$probe.source;frames=$probe.all.frames;frameIntervalMs=$probe.all.interval
  over11ms=$probe.all.over11ms;over22ms=$probe.all.over22ms
  renderBusyMs=$probe.all.render_work;gpuSpanMs=$probe.all.gpu_work
  gameThreadHookMs=$probe.tickSummary.hookMs;hostMs=$probe.tickSummary.hostMs
 }|ConvertTo-Json -Depth 4
}
Write-Host "Analysis saved in $run"
