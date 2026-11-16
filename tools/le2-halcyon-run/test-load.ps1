# SPDX-License-Identifier: MIT
# Offline tests for the existing load runner. No game, request files or personal saves touched.
$ErrorActionPreference = 'Stop'
Import-Module "$PSHOME\Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1" -ErrorAction Stop
Import-Module "$PSHOME\Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1" -ErrorAction Stop
. "$PSScriptRoot\session-common.ps1"
. "$PSScriptRoot\load-common.ps1"
$count = 0
function Check([bool]$Value) { if (-not $Value) { throw 'Assertion failed' } }
function Reject([scriptblock]$Action) { try { & $Action | Out-Null } catch { return }; throw 'Expected refusal' }
function Case([string]$Name, [scriptblock]$Action) { & $Action; $script:count++; "PASS $Name" }
Case 'runner and helpers parse' {
    foreach ($name in @('load', 'load-common', 'test-load')) {
        $tokens = $null; $errors = $null
        [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot "$name.ps1"), [ref]$tokens, [ref]$errors) | Out-Null
        Check (-not $errors)
    }
}
Case 'normal descriptor types and manual index' {
    $manual = Get-LoadDescriptor test_career 'Save_0031.pcsav'
    Check ($manual.index -eq 31 -and $manual.type -eq 0)
    foreach ($entry in @(@('QuickSave', 1), @('AutoSave', 2), @('ChapterSave', 3))) {
        $d = Get-LoadDescriptor test_career ($entry[0] + '.pcsav')
        Check ($d.index -eq -1 -and $d.type -eq $entry[1])
    }
}
Case 'imports paths injection and malformed types refused' {
    foreach ($name in @('LegendSave.pcsav', 'NewGamePlusSave.pcsav', 'Export.pcsav', 'Save_-1.pcsav', 'Save_999999999999.pcsav', 'Save_0031.txt', 'other\Save_0031.pcsav')) {
        Reject { Get-LoadDescriptor test $name }
    }
    foreach ($career in @('two words', 'test;quit', "test`nquit", '..', 'x/y', "test'name")) { Reject { Get-LoadDescriptor $career 'Save_0031.pcsav' } }
}
Case 'LE3 Legend checkpoint keeps its native descriptor and remains unavailable on LE2' {
    $d = Get-LoadDescriptor test_career 'LegendSave.pcsav' LE3
    Check ($d.name -ceq 'LegendSave' -and $d.index -eq -1 -and $d.type -eq 5)
    Check ((Get-LoadDescriptor test_career 'legendsave.PCSAV' LE3).name -ceq 'LegendSave')
    Reject { Get-LoadDescriptor test_career 'LegendSave.pcsav' LE2 }
    foreach ($name in @('Export.pcsav', 'NewGamePlusSave.pcsav', 'LegendSave_1.pcsav', 'other\LegendSave.pcsav')) {
        Reject { Get-LoadDescriptor test_career $name LE3 }
    }
    Check (-not (Test-LoadDescriptor $d (ConvertFrom-LoadDescriptor "mevr savedesc: career 'test_career' save 'LegendSave' index -1 type 4")))
}
Case 'exact descriptor matching does not trust queue acknowledgement' {
    $expected = Get-LoadDescriptor test_career 'Save_0031.pcsav'
    $text = "mevr savedesc: career 'test_career' save 'Save_0031' index 31 type 0"
    Check (Test-LoadDescriptor $expected (ConvertFrom-LoadDescriptor $text))
    Check (-not (Test-LoadDescriptor $expected (ConvertFrom-LoadDescriptor ($text -replace 'index 31', 'index 32'))))
    Check (-not (Test-LoadDescriptor $expected (ConvertFrom-LoadDescriptor ($text -replace 'test_career', 'other'))))
    Check (-not (ConvertFrom-LoadDescriptor "mevr load: queued career 'test_career' save 'Save_0031' index 31 type 0"))
}
$legacy = 'mevr state: world World BioP_Nor.TheWorld; pawn BioPawn BioP_Nor.Player at (1, 2, 3); game mode 0'
Case 'legacy LE2 preserved while LE3 needs identities' {
    Check (Test-LoadGameplay (ConvertFrom-LoadState $legacy LE2))
    Check (-not (ConvertFrom-LoadState $legacy LE3))
    Check (-not (ConvertFrom-LoadState ($legacy + '; junk') LE2))
}
$one = ConvertFrom-LoadState ($legacy + '; worldId 0x100; pawnId 0x200') LE3
Case 'same-world native reload needs observed identity change' {
    $same = ConvertFrom-LoadState ($legacy + '; worldId 0X100; pawnId 0x200') LE3
    Check (-not (Test-LoadTransition $one $same LE3))
    $new = ConvertFrom-LoadState ($legacy + '; worldId 0x300; pawnId 0x400') LE3
    Check (Test-LoadTransition $one $new LE3)
    Check (Test-LoadGameplay $new)
    Check (-not (Test-LoadTransition (ConvertFrom-LoadState $legacy LE2) (ConvertFrom-LoadState $legacy LE2) LE2))
    Check (Test-LoadTransition (ConvertFrom-LoadState ($legacy -replace 'BioP_Nor', 'BioP_Title') LE2) (ConvertFrom-LoadState $legacy LE2) LE2)
}
Case 'transient absence is evidence of transition but never gameplay' {
    $empty = ConvertFrom-LoadState 'mevr state: world None; pawn None; game mode none; worldId 0x0; pawnId 0x0' LE3
    Check (Test-LoadTransition $one $empty LE3)
    Check (-not (Test-LoadGameplay $empty))
    Check (-not (Test-LoadGameplay (ConvertFrom-LoadState ($legacy -replace 'mode 0', 'mode 10') LE2)))
    Check (-not (Test-LoadGameplay (ConvertFrom-LoadState ($legacy + '; worldId 0x100; pawnId 0x0') LE3)))
}
Case 'LE3 mode flags must be actual true booleans' {
    $session = [pscustomobject]@{ desktopDump = $true; engineProbe = $true; engineRequests = $true }
    Assert-LoadMode $session LE3
    foreach ($flag in @('desktopDump', 'engineProbe', 'engineRequests')) {
        $session.$flag = 'true'; Reject { Assert-LoadMode $session LE3 }
        $session.$flag = $false; Reject { Assert-LoadMode $session LE3 }
        $session.$flag = $true
    }
    Reject { Assert-LoadMode ([pscustomobject]@{}) LE3 }
    Assert-LoadMode ([pscustomobject]@{}) LE2
}
Case 'wrong game reused PID wrong path and changed start refused' {
    $when = [DateTime]::Parse('2026-01-01T00:00:00Z').ToUniversalTime()
    $record = [pscustomobject]@{ pid = 42; path = 'C:\synthetic\MassEffect3.exe'; startedUtc = $when.ToString('o') }
    $process = [pscustomobject]@{ Id = 42; Path = $record.path; StartTime = $when }
    Assert-LoadProcessSet @($process) $record $record.path
    Reject { Assert-LoadProcessSet @() $record $record.path }
    Reject { Assert-LoadProcessSet @($process, $process) $record $record.path }
    Reject { Assert-LoadProcessSet @($process) $record 'C:\synthetic\MassEffect2.exe' }
    $process.StartTime = $when.AddSeconds(1)
    Reject { Assert-LoadProcessSet @($process) $record $record.path }
    $process.StartTime = $when; $process.Id = 43
    Reject { Assert-LoadProcessSet @($process) $record $record.path }
}
Case 'single request echo rejects stale or unrelated reply' {
    Check ((ConvertFrom-LoadReply "> mevr state`n$legacy`n" 'mevr state') -eq $legacy)
    Reject { ConvertFrom-LoadReply "> mevr savedesc`n$legacy" 'mevr state' }
    Reject { ConvertFrom-LoadReply "> mevr state`n$legacy`n> quit`nOK" 'mevr state' }
    Reject { ConvertFrom-LoadReply $legacy 'mevr state' }
}

Case 'preview load requires explicit mode and matching JSON booleans' {
    $s=[pscustomobject]@{desktopDump=$false;engineProbe=$false;engineRequests=$false;rendererPreview=$true}
    Assert-LoadMode $s LE3 $true
    Reject { Assert-LoadMode $s LE3 }
    Reject { Assert-LoadMode $s LE2 $true }
    foreach ($flag in @('desktopDump','engineProbe','engineRequests','rendererPreview')) {
        $old=$s.$flag; $s.$flag='false'; Reject { Assert-LoadMode $s LE3 $true }; $s.$flag=$old
    }
    $s.engineRequests=$true; Reject { Assert-LoadMode $s LE3 $true }
}

"$count load tests passed"
