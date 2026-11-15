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
function Reject([scriptblock]$Action) { try { & $Action | Out-Null } catch { return }; throw "Expected refusal: $Action" }
function Case([string]$Name, [scriptblock]$Action) { & $Action; $script:count++; "PASS $Name" }
function With-ProcessObservations([object[]]$Sequence, [scriptblock]$Action) {
    $observations = New-Object Collections.Queue
    foreach ($entry in $Sequence) { $observations.Enqueue($entry) }
    function Get-Process {
        param([int]$Id, $ErrorAction)
        if ($Id -ne 42 -or -not $observations.Count) { throw 'Unexpected process lookup in synthetic test' }
        $observations.Dequeue()
    }
    & $Action
    Check ($observations.Count -eq 0)
}
$base = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\build-runner-tests'))
$root = Join-Path $base ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $root | Out-Null
try {
    Case 'live host log permits shared reader without closing writer' {
        $logPath = Join-Path $root 'live-host.log'
        $writer = [IO.File]::Open($logPath, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes('dump-only: code dump attempted; renderer and hooks disabled')
            $writer.Write($bytes, 0, $bytes.Length); $writer.Flush()
            Reject { [IO.File]::ReadAllText($logPath) }
            Check ((Read-RunLog $logPath) -eq [Text.Encoding]::UTF8.GetString($bytes))
        } finally { $writer.Dispose() }
    }
    Case 'scripts parse in Windows PowerShell' {
        foreach ($name in @('session-common', 'run-lock', 'prep', 'launch', 'restore', 'load', 'load-common', '..\vr-game-settings')) {
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
    Case 'engine probe is explicit and cannot change an open mode' {
        Assert-RunMode LE3 $true $true
        Reject { Assert-RunMode LE2 $false $true }
        $session = [pscustomobject]@{ holder = 'owner'; game = 'LE3'; desktopDump = $true; engineProbe = $true }
        Assert-RunSession $session owner LE3 $true $true
        Reject { Assert-RunSession $session owner LE3 $true $false }
        $session.engineProbe = 'true'
        Reject { Assert-RunSession $session owner LE3 $true $true }
        Assert-RunDesktopConfig "[Debug]`nengineProbe = 1" $true
        Assert-RunDesktopConfig "[Debug]`ndumpOnly = 1" $false
        Reject { Assert-RunDesktopConfig "[Debug]`ndumpOnly = 1" $true }
        Reject { Assert-RunDesktopConfig "[Debug]`nengineProbe = 1`ndumpOnly = 1" $true }
    }
    Case 'reflection acceptance needs fresh consistent rows and no mismatches' {
        $path = Join-Path $root 'reflect.json'
        $began = [DateTime]::UtcNow.AddSeconds(-5)
        $validReport = '{"checks":2,"agree":1,"mismatches":0,"notReflected":1,"rows":[{"agree":true,"notReflected":false},{"agree":false,"notReflected":true}]}'
        [IO.File]::WriteAllText($path, $validReport)
        $receipt = Confirm-RunReflection $path $began
        Check ($receipt.agree -eq 1 -and $receipt.notReflected -eq 1)
        Reject { Confirm-RunReflection $path ([DateTime]::UtcNow.AddMinutes(1)) }
        [IO.File]::WriteAllText($path, $validReport.Replace('"agree":1', '"agree":2'))
        Reject { Confirm-RunReflection $path $began }
        [IO.File]::WriteAllText($path, $validReport.Replace('"mismatches":0', '"mismatches":1').Replace('"notReflected":1', '"notReflected":0').Replace('"notReflected":true', '"notReflected":false'))
        Reject { Confirm-RunReflection $path $began }
        [IO.File]::WriteAllText($path, '{}')
        Reject { Confirm-RunReflection $path $began }
    }
    Case 'request consumer needs explicit owned engine-request mode' {
        Assert-RunMode LE3 $true $true $true
        Reject { Assert-RunMode LE3 $true $false $true }
        $session = [pscustomobject]@{ holder = 'owner'; game = 'LE3'; desktopDump = $true; engineProbe = $true; engineRequests = $true }
        Assert-RunSession $session owner LE3 $true $true $true
        Reject { Assert-RunSession $session owner LE3 $true $true $false }
        $session.engineRequests = 'true'
        Reject { Assert-RunSession $session owner LE3 $true $true $true }
        Assert-RunDesktopConfig "[Debug]`nengineProbe = 1`nengineProbeRequests = 1" $true $true
        Reject { Assert-RunDesktopConfig "[Debug]`nengineProbe = 1" $true $true }
        Reject { Assert-RunDesktopConfig "[Debug]`nengineProbe = 1`nengineProbeRequests = 1" $true $false }
    }
    Case 'stale console requests are preserved and restored across redeploy' {
        $channel = Join-Path $root 'channel'; $sessionDir = Join-Path $root 'session'
        New-Item -ItemType Directory -Path $channel, $sessionDir | Out-Null
        [IO.File]::WriteAllText((Join-Path $channel 'console.request'), 'original request')
        $session = [pscustomobject]@{ session = $sessionDir }
        $currentFile = Join-Path $root 'current.json'
        Backup-RunConsole $session $currentFile $channel
        Check (-not (Test-Path -LiteralPath (Join-Path $channel 'console.request')))
        $session = Get-Content -LiteralPath $currentFile -Raw | ConvertFrom-Json
        [IO.File]::WriteAllText((Join-Path $channel 'console.request'), 'later request')
        [IO.File]::WriteAllText((Join-Path $channel 'console.result'), 'later result')
        Backup-RunConsole $session $currentFile $channel
        Restore-RunConsole $session $channel
        Check ([IO.File]::ReadAllText((Join-Path $channel 'console.request')) -eq 'original request')
        Check (-not (Test-Path -LiteralPath (Join-Path $channel 'console.result')))
        [IO.File]::WriteAllText((Join-Path $sessionDir 'console-before\console.request'), 'corrupt backup')
        Reject { Restore-RunConsole $session $channel }
        Check ([IO.File]::ReadAllText((Join-Path $channel 'console.request')) -eq 'original request')
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
    $requested = [DateTime]::UtcNow
    $expected = 'C:\synthetic\MassEffectLauncher.exe'
    $liveProcess = [pscustomobject]@{ Id = 42; Path = $expected; StartTime = $requested.AddSeconds(1); HasExited = $false }
    $unreadableProcess = [pscustomobject]@{ Id = 42; Path = $null; StartTime = $null; HasExited = $false }
    Case 'launcher disappearing before or during identity read is skipped' {
        With-ProcessObservations @($null) { Check ($null -eq (Get-RunLaunchIdentity 42 $expected $requested)) }
        With-ProcessObservations @($unreadableProcess, $null) { Check ($null -eq (Get-RunLaunchIdentity 42 $expected $requested)) }
        $exited = [pscustomobject]@{ Id = 42; Path = $null; StartTime = $null; HasExited = $true }
        With-ProcessObservations @($liveProcess, $exited) { Check ($null -eq (Get-RunLaunchIdentity 42 $expected $requested)) }
    }
    Case 'live launcher must have the expected path and launch creation time' {
        With-ProcessObservations @($liveProcess, $liveProcess) {
            $identity = Get-RunLaunchIdentity 42 $expected $requested
            Check ($identity.pid -eq 42 -and $identity.path -eq $expected -and $identity.startedUtc -eq $liveProcess.StartTime.ToString('o'))
        }
        $wrong = [pscustomobject]@{ Id = 42; Path = 'C:\other\launcher.exe'; StartTime = $requested; HasExited = $false }
        With-ProcessObservations @($wrong, $wrong) {
            $message = ''
            try { Get-RunLaunchIdentity 42 $expected $requested } catch { $message = $_.Exception.Message }
            Check ($message -like '*Unexpected live process*pid=42*' -and $message.Contains($wrong.Path) -and $message.Contains('startedUtc='))
        }
        $older = [pscustomobject]@{ Id = 42; Path = $expected; StartTime = $requested.AddMinutes(-1); HasExited = $false }
        With-ProcessObservations @($older, $older) { Reject { Get-RunLaunchIdentity 42 $expected $requested } }
    }
    Case 'unreadable live identity or PID replacement remains fail closed' {
        With-ProcessObservations @($unreadableProcess, $unreadableProcess) { Reject { Get-RunLaunchIdentity 42 $expected $requested } }
        $replacement = [pscustomobject]@{ Id = 42; Path = $expected; StartTime = $requested.AddSeconds(2); HasExited = $false }
        With-ProcessObservations @($liveProcess, $replacement) { Reject { Get-RunLaunchIdentity 42 $expected $requested } }
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
        Reject { Assert-RunDumpHost "$root\host.asi" $true }
        [IO.File]::WriteAllText("$root\host.asi", 'engine-probe: host renderer disabled')
        Assert-RunDumpHost "$root\host.asi" $true
        Reject { Assert-RunDumpHost "$root\host.asi" $false }
    }
    Case 'preview is explicit LE3-only mode and rejects mixed or false metadata' {
        Assert-RunMode LE3 $false $false $false $true
        foreach ($flags in @(@($true,$false,$false), @($false,$true,$false), @($false,$false,$true))) {
            Reject { Assert-RunMode LE3 $flags[0] $flags[1] $flags[2] $true }
        }
        Reject { Assert-RunMode LE2 $false $false $false $true }
        $session = [pscustomobject]@{holder='owner';game='LE3';desktopDump=$false;engineProbe=$false;engineRequests=$false;rendererPreview=$true}
        Assert-RunSession $session owner LE3 $false $false $false $true
        Reject { Assert-RunSession $session intruder LE3 $false $false $false $true }
        Reject { Assert-RunSession $session owner LE3 $true $true $true }
        foreach ($field in @('desktopDump','engineProbe','engineRequests','rendererPreview')) {
            $saved = $session.$field; $session.$field = 'false'
            Reject { Assert-RunSession $session owner LE3 $false $false $false $true }
            $session.$field = $saved
        }
        $session.PSObject.Properties.Remove('engineRequests')
        Reject { Assert-RunSession $session owner LE3 $false $false $false $true }
        foreach ($script in @('prep','launch','restore')) {
            Reject { & "$PSScriptRoot\$script.ps1" -Holder synthetic -Game LE3 -RendererPreview -DesktopDump }
        }
    }
    Case 'preview request quarantine preserves replay input bytes and original absence' {
        $channel = Join-Path $root 'preview-channel'; $sessionDir = Join-Path $root 'preview-channel-session'
        New-Item -ItemType Directory $channel,$sessionDir | Out-Null
        $request = Join-Path $channel 'replay.request'; $snapshot = Join-Path $channel 'snapshot.request'
        [IO.File]::WriteAllBytes($request, [byte[]]@(239,187,191,105,110,112,117,116,115,61,49,13,10))
        $hash = (Get-FileHash -LiteralPath $request).Hash
        [IO.File]::WriteAllText((Join-Path $channel 'unknown.request'), 'not ours')
        $session = [pscustomobject]@{session=$sessionDir}; $currentFile=Join-Path $root 'preview-channel-current.json'
        Backup-RunConsole $session $currentFile $channel -Names (Get-PreviewChannels) -Metadata previewChannelsBefore -Directory preview-channels-before
        Check (-not (Test-Path -LiteralPath $request))
        [IO.File]::WriteAllText($request,'a different replay with inputs'); [IO.File]::WriteAllText($snapshot,'capture')
        Backup-RunConsole $session $currentFile $channel -Names (Get-PreviewChannels) -Metadata previewChannelsBefore -Directory preview-channels-before
        Check (-not (Test-Path -LiteralPath $request))
        Restore-RunConsole $session $channel -Names (Get-PreviewChannels) -Metadata previewChannelsBefore -Directory preview-channels-before
        Check ((Get-FileHash -LiteralPath $request).Hash -eq $hash)
        Check (-not (Test-Path -LiteralPath $snapshot))
        Check ([IO.File]::ReadAllText((Join-Path $channel 'unknown.request')) -eq 'not ours')
    }
    Case 'preview exact environment restores bytes absence and rejects corrupt backup before mutation' {
        $envRoot=Join-Path $root 'environment'; $sessionDir=Join-Path $root 'env-session'
        New-Item -ItemType Directory $envRoot,$sessionDir | Out-Null
        $paths=[ordered]@{null=(Join-Path $envRoot 'null.json');steam=(Join-Path $envRoot 'steam.json');chaperone=(Join-Path $envRoot 'chap.json');le1=(Join-Path $envRoot 'le1.ini')}
        [IO.File]::WriteAllText($paths.null,'{"driver_null":{"enable":false}}')
        [IO.File]::WriteAllText($paths.steam,' { "custom" : 12 } ')
        [IO.File]::WriteAllText($paths.le1,"[Debug]`r`nopenxrRuntime = OriginalXR`r`n")
        $rows=@(Backup-PreviewEnvironment $sessionDir $paths)
        $session=[pscustomobject]@{session=$sessionDir;previewEnvironment=$rows}
        foreach ($path in $paths.Values) { [IO.File]::WriteAllText($path,'changed by fake session') }
        Restore-PreviewEnvironment $session $paths
        Check (-not (Test-Path -LiteralPath $paths.chaperone))
        Check ([IO.File]::ReadAllText($paths.steam) -ceq ' { "custom" : 12 } ')
        Check ([IO.File]::ReadAllText($paths.le1).Contains('OriginalXR'))
        $before=(Get-FileHash -LiteralPath $paths.null).Hash
        [IO.File]::WriteAllText((Join-Path $sessionDir 'preview-environment\steam'),'corrupt')
        Reject { Restore-PreviewEnvironment $session $paths }
        Check ((Get-FileHash -LiteralPath $paths.null).Hash -eq $before)
    }
    Case 'preview refuses an existing fake owner and running VR without changing files' {
        $envRoot=Join-Path $root 'idle-environment'; New-Item -ItemType Directory $envRoot | Out-Null
        $paths=[ordered]@{null=(Join-Path $envRoot 'null.json');steam=(Join-Path $envRoot 'steam.json')}
        [IO.File]::WriteAllText($paths.null,'{"driver_null":{"enable":false}}')
        [IO.File]::WriteAllText($paths.steam,'{"steamvr":{}}')
        $undo=Join-Path $envRoot 'undo.json'
        function Get-Process { param($Name,$ErrorAction); return $null }
        Assert-PreviewIdle $paths $undo
        [IO.File]::WriteAllText($undo,'{}'); Reject { Assert-PreviewIdle $paths $undo }; Remove-Item -LiteralPath $undo
        [IO.File]::WriteAllText($paths.steam,'{"steamvr":{"forcedDriver":"null"}}'); Reject { Assert-PreviewIdle $paths $undo }
        [IO.File]::WriteAllText($paths.steam,'{"steamvr":{}}')
        function Get-Process { param($Name,$ErrorAction); return [pscustomobject]@{Id=99} }
        Reject { Assert-PreviewIdle $paths $undo }
    }
    Case 'preview configuration and artifact hashes bind actual host and module' {
        Check ((Get-PreviewModConfig) -match '(?s)\[Pointer\]\s+setCursor\s*=\s*0')
        $bin=Join-Path $root 'preview-bin'; New-Item -ItemType Directory (Join-Path $bin 'ASI') | Out-Null
        [IO.File]::WriteAllText((Join-Path $bin 'ASI\LE3MEVR.ini'),(Get-PreviewModConfig))
        [IO.File]::WriteAllText((Join-Path $bin 'MELE3VR.ini'),(Get-PreviewRendererConfig))
        [IO.File]::WriteAllText((Join-Path $bin 'MassEffect3.exe'),'synthetic exe')
        [IO.File]::WriteAllText((Join-Path $bin 'ASI\LE3MEVR.asi'),'synthetic host')
        $module=Join-Path $bin 'module.dll'; [IO.File]::WriteAllText($module,'synthetic module')
        [IO.File]::WriteAllText((Join-Path $bin 'ASI\LE3MEVR.modpath'),$module)
        Assert-PreviewConfig $bin
        $artifacts=@(Get-PreviewArtifacts $bin); Check ($artifacts.Count -eq 6); Assert-RunArtifacts $artifacts
        [IO.File]::WriteAllText($module,'new module'); Reject { Assert-RunArtifacts $artifacts }
        [IO.File]::AppendAllText((Join-Path $bin 'ASI\LE3MEVR.ini'),"`r`nengineProbe=1")
        Reject { Assert-PreviewConfig $bin }
        Check ((Get-RunRendererLog LE3).EndsWith('MELEVR\ME3PassiveDxgiProbe.log'))
    }
    Case 'renderer readiness marker must be fresh rather than replayed old log content' {
        $path=Join-Path $root 'preview.log'; $marker='[ME3XR] VR session ready; starting frame submission.'
        [IO.File]::WriteAllText($path,$marker); $started=[DateTime]::UtcNow.AddSeconds(-1)
        Check (-not (Test-RunFreshMarker $path $marker $started $marker))
        [IO.File]::AppendAllText($path,"`nnew unrelated line")
        Check (-not (Test-RunFreshMarker $path $marker $started $marker))
        [IO.File]::AppendAllText($path,"`n$marker")
        Check (Test-RunFreshMarker $path $marker $started $marker)
        Check (-not (Test-RunFreshMarker $path '' ([DateTime]::UtcNow.AddMinutes(1)) $marker))
    }
    Case 'preview VR adoption rejects replaced PID time path and unowned helpers' {
        $when=[DateTime]::UtcNow
        $server=[pscustomobject]@{Id=100;ProcessName='vrserver';Path='C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrserver.exe';StartTime=$when;HasExited=$false}
        $processSet=@($server)
        function Get-Process {
            param($Name,[int]$Id,$ErrorAction)
            if ($PSBoundParameters.ContainsKey('Id')) { return @($processSet | Where-Object { $_.Id -eq $Id }) }
            return $processSet
        }
        $session=[pscustomobject]@{previewStartedUtc=$when.AddSeconds(-1).ToString('o')}
        $recorded=@(Get-PreviewVrProcesses $session $true); Check ($recorded.Count -eq 1)
        $session | Add-Member -NotePropertyName previewProcesses -NotePropertyValue $recorded
        Check (@(Get-PreviewVrProcesses $session).Count -eq 1)
        $server.Id=101; Reject { Get-PreviewVrProcesses $session $true }; $server.Id=100
        $server.StartTime=$when.AddSeconds(1); Reject { Get-PreviewVrProcesses $session $true }; $server.StartTime=$when
        $oldPath=$server.Path; $server.Path='C:\other\vrserver.exe'; Reject { Get-PreviewVrProcesses $session $true }; $server.Path=$oldPath
        $helper=[pscustomobject]@{Id=102;ProcessName='vrcompositor';Path='C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrcompositor.exe';StartTime=$when;HasExited=$false}
        $processSet=@($server,$helper)
        Reject { Get-PreviewVrProcesses $session }
        Check (@(Get-PreviewVrProcesses $session $true).Count -eq 2)
        $processSet=@($helper); Reject { Get-PreviewVrProcesses $session $true }
        $processSet=@($server,$server); Reject { Get-PreviewVrProcesses $session $true }
    }
    "$count offline runner tests passed"
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    if (-not $resolved.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
