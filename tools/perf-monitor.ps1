# Samples GPU and CPU load while the game runs, to tell a GPU-bound frame rate from a CPU-bound one.
# Halcyon's log has the frame rate but not where the time goes (its [GPU] figure is the frame
# interval). Every -IntervalSeconds while the game process lives:
#   gpu          whole-GPU utilisation from nvidia-smi (%), all processes: the game, the VR
#                compositor and the streaming encoder
#   gpu_clock    graphics clock (MHz): a low clock with low utilisation means the GPU is idling
#   cpu_game     the game's CPU use, as a share of the whole machine (%)
#   top_thread   the busiest game thread (% of one core) and its id: near 100 means that thread
#   second       is the limit; the next busiest for comparison
# Near-100% GPU = GPU-bound; a game thread near 100% with the GPU well below = CPU-bound.
#
# Writes %LOCALAPPDATA%\MEVR\perf\perf-<time>.csv and prints medians when the game exits (or Ctrl+C).
# Usage: tools/perf-monitor.ps1 [-ProcessName MassEffect1] [-IntervalSeconds 1]
param(
    [string]$ProcessName = 'MassEffect1',
    [double]$IntervalSeconds = 1,
    [int]$MaxSamples = 0  # 0 = until the process exits
)

$ErrorActionPreference = 'Continue'
$outDir = Join-Path $env:LOCALAPPDATA 'MEVR\perf'
New-Item -ItemType Directory -Force $outDir | Out-Null
$csv = Join-Path $outDir ("perf-{0:yyyyMMdd-HHmmss}.csv" -f (Get-Date))
$smi = Join-Path $env:WINDIR 'System32\nvidia-smi.exe'
$haveSmi = Test-Path $smi

Write-Output "Waiting for $ProcessName..."
$process = $null
while (-not $process) {
    $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $process) { Start-Sleep -Seconds 2 }
}
$cores = [Environment]::ProcessorCount
Write-Output "Sampling $ProcessName (pid $($process.Id)) every $IntervalSeconds s -> $csv"
'time,gpu,gpu_clock,cpu_game,top_thread,top_thread_id,second_thread' | Set-Content -Encoding utf8 $csv

function Median([double[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return 'n/a' }
    $sorted = $values | Sort-Object
    return $sorted[[int][math]::Floor(($sorted.Count - 1) / 2)]
}

function ThreadTimes($proc) {
    $times = @{}
    try {
        $proc.Refresh()
        foreach ($t in $proc.Threads) {
            try { $times[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {}
        }
    } catch {}
    return $times
}

$rows = New-Object System.Collections.Generic.List[object]
$count = 0
try {
    $before = ThreadTimes $process
    $processBefore = $process.TotalProcessorTime.TotalMilliseconds
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $process.HasExited) {
        Start-Sleep -Milliseconds ([int]($IntervalSeconds * 1000))
        if ($process.HasExited) { break }
        $elapsed = $clock.Elapsed.TotalMilliseconds
        $clock.Restart()
        $after = ThreadTimes $process
        $processAfter = $process.TotalProcessorTime.TotalMilliseconds
        $loads = foreach ($id in $after.Keys) {
            if ($before.ContainsKey($id)) { [pscustomobject]@{ Id = $id; Pct = 100.0 * ($after[$id] - $before[$id]) / $elapsed } }
        }
        $sorted = @($loads | Sort-Object Pct -Descending)
        $top = if ($sorted.Count -gt 0) { $sorted[0] } else { [pscustomobject]@{ Id = 0; Pct = 0 } }
        $second = if ($sorted.Count -gt 1) { $sorted[1].Pct } else { 0 }
        $cpu = 100.0 * ($processAfter - $processBefore) / $elapsed / $cores
        $before = $after
        $processBefore = $processAfter

        $gpu = ''; $gpuClock = ''
        if ($haveSmi) {
            $smiOut = & $smi --query-gpu=utilization.gpu,clocks.gr --format=csv,noheader,nounits 2>$null | Select-Object -First 1
            if ($smiOut) { $parts = $smiOut -split ','; $gpu = $parts[0].Trim(); $gpuClock = $parts[1].Trim() }
        }
        $row = [pscustomobject]@{ time = (Get-Date).ToString('HH:mm:ss'); gpu = $gpu; gpu_clock = $gpuClock;
            cpu_game = [math]::Round($cpu, 1); top_thread = [math]::Round($top.Pct, 1); top_thread_id = $top.Id;
            second_thread = [math]::Round($second, 1) }
        $rows.Add($row)
        "$($row.time),$($row.gpu),$($row.gpu_clock),$($row.cpu_game),$($row.top_thread),$($row.top_thread_id),$($row.second_thread)" |
            Add-Content -Encoding utf8 $csv
        $count++
        if ($MaxSamples -gt 0 -and $count -ge $MaxSamples) { break }
    }
} finally {
    if ($rows.Count -gt 0) {
        $gpuValues = @($rows | Where-Object { $_.gpu -ne '' } | ForEach-Object { [double]$_.gpu })
        Write-Output ("{0} samples. Medians: GPU {1}%, game CPU {2}% of the machine, busiest game thread {3}% of a core, next {4}%" -f
            $rows.Count, (Median $gpuValues), (Median ($rows | ForEach-Object { [double]$_.cpu_game })),
            (Median ($rows | ForEach-Object { [double]$_.top_thread })), (Median ($rows | ForEach-Object { [double]$_.second_thread })))
        Write-Output "CSV: $csv"
    }
}
