# SPDX-License-Identifier: MIT
<#
Read-only by default. -Launch requires an owned prepared session, a verified desktop
host, an existing Steam outside a job, and reviewed installed ME2 launcher support.
Process binding is not save selection or gameplay readiness. See launch.md.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunDirectory,
    [Parameter(Mandatory)][string]$SteamExe,
    [string]$CapabilityReceipt,
    [switch]$Plan,
    [switch]$Launch,
    [ValidateRange(1,180)][int]$TimeoutSeconds = 120,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
if ($Plan -and $Launch) { throw '-Plan and -Launch are mutually exclusive.' }
$mode = if ($Launch) { 'launch' } else { 'plan' }
$arguments = @('-B', (Join-Path $PSScriptRoot 'launch_support.py'), $mode,
    '--run', $RunDirectory, '--steam-exe', $SteamExe, '--timeout', "$TimeoutSeconds")
if ($CapabilityReceipt) { $arguments += @('--capability-receipt', $CapabilityReceipt) }
& $Python @arguments
exit $LASTEXITCODE
