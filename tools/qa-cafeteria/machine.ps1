# SPDX-License-Identifier: MIT
# Optional read-only machine receipt. Run before, not during, a timing capture.
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$receipt=@{collected=(Get-Date).ToString('o');
    cpu=@(Get-CimInstance Win32_Processor|Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);
    gpu=@(Get-CimInstance Win32_VideoController|Select-Object Name,DriverVersion);
    os=Get-CimInstance Win32_OperatingSystem|Select-Object Caption,BuildNumber;
    powerScheme=(& powercfg.exe /getactivescheme)-join ' '}
$smi=Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
if($smi){$receipt.nvidia=(& $smi.Source --query-gpu=name,driver_version,pstate,power.limit,clocks.max.graphics,clocks.max.memory --format=csv,noheader)-join "`n"}
$receipt|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'machine.json')
