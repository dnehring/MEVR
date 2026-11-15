# SPDX-License-Identifier: MIT
# Offline synthetic safety tests. Never launches, deploys, touches game saves, or changes VR settings.
$ErrorActionPreference = 'Stop'
# CTest may inherit PowerShell 7's PSModulePath while executing Windows PowerShell 5.
# Import this process's built-in module explicitly; never change machine/user module paths.
Import-Module "$PSHOME\Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1" -ErrorAction Stop
Import-Module "$PSHOME\Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1" -ErrorAction Stop
. "$PSScriptRoot\session-common.ps1"
$count = 0
function Check([bool]$Value) { if (-not $Value) { throw 'Assertion failed' } }
function Reject([scriptblock]$Action) { try { & $Action | Out-Null } catch { return }; throw 'Expected refusal' }
function Case([string]$Name, [scriptblock]$Action) { & $Action; $script:count++; "PASS $Name" }
$base = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\build-runner-tests'))
$root = Join-Path $base ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $root | Out-Null
try {
    Case 'scripts parse in Windows PowerShell' {
        foreach ($name in @('session-common', 'run-lock', 'prep', 'launch', 'restore')) {
            $tokens = $null; $errors = $null
            [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot "$name.ps1"), [ref]$tokens, [ref]$errors) | Out-Null
            Check (-not $errors)
        }
    }
    Case 'game-specific names and current files' {
        $a = Get-RunNames LE2; $b = Get-RunNames LE3
        Check ($a.process -eq 'MassEffect2' -and $b.process -eq 'MassEffect3' -and $a.current -ne $b.current)
        Reject { Get-RunNames LE1 }
    }
    Case 'valid modes and false switch rejection' {
        Assert-RunMode LE2 $false; Assert-RunMode LE3 $true
        Reject { Assert-RunMode LE3 $false }; Reject { Assert-RunMode LE2 $true }
        foreach ($script in @('prep', 'launch', 'restore')) {
            Reject { & "$PSScriptRoot\$script.ps1" -Holder synthetic -Game LE3 -DesktopDump:$false }
        }
    }
    Case 'legacy LE2 ownership only' {
        $legacy = [pscustomobject]@{ holder = 'owner' }
        Assert-RunSession $legacy owner LE2 $false
        Reject { Assert-RunSession $legacy intruder LE2 $false }
        Reject { Assert-RunSession $legacy owner LE3 $true }
    }
    Case 'explicit wrong game and malformed false strings rejected' {
        $session = [pscustomobject]@{ holder = 'owner'; game = 'LE3'; desktopDump = $true }
        Assert-RunSession $session owner LE3 $true
        Reject { Assert-RunSession $session owner LE2 $false }
        $session.desktopDump = 'false'
        Reject { Assert-RunSession $session owner LE3 $true }
    }
    Case 'paths reject escapes and retain legacy manifest relatives' {
        Check ((Resolve-RunChild $root '\child') -eq "$root\child")
        Check ((Resolve-RunChild $root "$root\child") -eq "$root\child")
        Reject { Resolve-RunChild $root '..\escape' }
        Reject { Resolve-RunChild $root $base }
        Reject { Resolve-RunChild $root ($root + '-sibling\x') }
    }
    Case 'PID path and creation time all required' {
        $p = [pscustomobject]@{ Id = 42; Path = 'C:\test\MassEffect3.exe'; StartTime = [DateTime]::UtcNow }
        $record = Get-RunProcessIdentity $p
        Assert-RunProcess $p $record $p.Path
        Reject { Assert-RunProcess $p $null $p.Path }
        Reject { Assert-RunProcess $p $record 'C:\test\MassEffect2.exe' }
        $p.Id = 43; Reject { Assert-RunProcess $p $record $p.Path }; $p.Id = 42
        $p.StartTime = $p.StartTime.AddSeconds(1); Reject { Assert-RunProcess $p $record $p.Path }
    }
    $live = Join-Path $root 'live'; $backup = Join-Path $root 'backup'; $manifest = Join-Path $root 'before.json'
    New-Item -ItemType Directory $live | Out-Null
    [IO.File]::WriteAllText("$live\save[1].pcsav", 'synthetic save bytes')
    Case 'backup verifies literal filenames and publishes complete manifest' {
        Backup-RunRoot $live $backup $manifest
        Assert-RunBackup $live $backup $manifest
        Check ((Get-FileHash -LiteralPath "$live\save[1].pcsav").Hash -eq (Get-FileHash -LiteralPath "$backup\save[1].pcsav").Hash)
    }
    Case 'incomplete or altered backup rejected before restore' {
        $original = [IO.File]::ReadAllText($manifest)
        [IO.File]::WriteAllText($manifest, '[]'); Reject { Assert-RunBackup $live $backup $manifest }
        [IO.File]::WriteAllText($manifest, $original)
        [IO.File]::WriteAllText("$backup\save[1].pcsav", 'changed'); Reject { Assert-RunBackup $live $backup $manifest }
        [IO.File]::WriteAllText("$backup\save[1].pcsav", 'synthetic save bytes')
    }
    Case 'failed robocopy never publishes backup manifest' {
        function robocopy { $global:LASTEXITCODE = 8 }
        try { Reject { Backup-RunRoot $live "$root\failed" "$root\failed.json" }; Check (-not (Test-Path "$root\failed.json")) }
        finally { Remove-Item Function:\robocopy }
    }
    Case 'empty source directory has a valid empty manifest' {
        New-Item -ItemType Directory "$root\empty" | Out-Null
        Backup-RunRoot "$root\empty" "$root\empty-backup" "$root\empty.json"
        Assert-RunBackup "$root\empty" "$root\empty-backup" "$root\empty.json"
    }
    $exe = Join-Path $root 'fake.exe'
    $bytes = New-Object byte[] 512
    [BitConverter]::GetBytes([uint16]0x5A4D).CopyTo($bytes, 0)
    [BitConverter]::GetBytes([int]64).CopyTo($bytes, 60)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($bytes, 64)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 70)
    [BitConverter]::GetBytes([uint32]0x12345678).CopyTo($bytes, 72)
    [BitConverter]::GetBytes([uint16]240).CopyTo($bytes, 84)
    [Text.Encoding]::ASCII.GetBytes('.text').CopyTo($bytes, 328)
    [BitConverter]::GetBytes([uint32]32).CopyTo($bytes, 336)
    [BitConverter]::GetBytes([uint32]4096).CopyTo($bytes, 340)
    [IO.File]::WriteAllBytes($exe, $bytes)
    $meta = Join-Path $root 'MassEffect-12345678.txt'
    $binary = Join-Path $root 'MassEffect-12345678.text.bin'
    $valid = "timestamp=0x12345678`ntext_rva=0x1000`ntext_size=0x20`npatched_starts=0`npatched=`n"
    [IO.File]::WriteAllText($meta, $valid)
    [IO.File]::WriteAllBytes($binary, (New-Object byte[] 32))
    Case 'dump receipt binds installed exe header and both artifact hashes' {
        $r = Confirm-RunDump $exe $root
        Check ($r.exeSha256 -eq (Get-FileHash $exe).Hash -and $r.binarySha256 -eq (Get-FileHash $binary).Hash -and $r.patchedStarts -eq 0)
    }
    Case 'wrong-build metadata and duplicate fields rejected' {
        [IO.File]::WriteAllText($meta, $valid.Replace('0x12345678', '0x11111111')); Reject { Confirm-RunDump $exe $root }
        [IO.File]::WriteAllText($meta, $valid + 'text_size=0x20'); Reject { Confirm-RunDump $exe $root }
        [IO.File]::WriteAllText($meta, $valid)
    }
    Case 'patch count and RVAs must agree with the selected text section' {
        [IO.File]::WriteAllText($meta, $valid.Replace('patched_starts=0', 'patched_starts=1')); Reject { Confirm-RunDump $exe $root }
        [IO.File]::WriteAllText($meta, $valid.Replace('patched_starts=0', 'patched_starts=1').Replace('patched=', 'patched=0x2000')); Reject { Confirm-RunDump $exe $root }
        [IO.File]::WriteAllText($meta, $valid)
    }
    Case 'truncated dump and malformed executable rejected' {
        [IO.File]::WriteAllBytes($binary, (New-Object byte[] 31)); Reject { Confirm-RunDump $exe $root }
        $bytes[0] = 0; [IO.File]::WriteAllBytes($exe, $bytes); Reject { Get-RunTextIdentity $exe }
    }
    Case 'old host cannot bypass dump-only guard' {
        [IO.File]::WriteAllText("$root\host.asi", 'old host'); Reject { Assert-RunDumpHost "$root\host.asi" }
        [IO.File]::WriteAllText("$root\host.asi", 'dump-only: code dump attempted; renderer and hooks disabled')
        Assert-RunDumpHost "$root\host.asi"
    }
    "$count offline runner tests passed"
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    if (-not $resolved.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
