# SPDX-License-Identifier: MIT
# The existing protected normal-load flow for LE2/LE3. No launch, deployment or save editing.
# LE2 keeps its default Freedom's Progress Save 4. LE3 requires an explicit existing Save.
param(
    [Parameter(Mandatory)][string]$Holder,
    [ValidateSet('LE2', 'LE3')][string]$Game = 'LE2',
    [string]$Save,
    [switch]$RendererPreview,
    [ValidateRange(5, 1800)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\session-common.ps1"
. "$PSScriptRoot\load-common.ps1"
. "$PSScriptRoot\..\common.ps1"
if ($Game -eq 'LE3' -and -not $Save) { throw 'LE3 loading requires explicit -Save; no default or newest-save inference' }
$names = Get-RunNames $Game
$desktop = $Game -eq 'LE3' -and -not $RendererPreview
$open = Read-RunSession $Holder $Game $desktop $desktop $desktop ([bool]$RendererPreview)
Assert-LoadMode $open $Game ([bool]$RendererPreview)
$install = (Find-MELEInstall).Root
if (-not $open.install -or $open.install -ne $install) { throw 'Install identity differs from the protected session' }
$bin = Get-MELEBinDir $install $Game
$exe = Join-Path $bin "$($names.process).exe"
$sessionHash = (Get-FileHash -LiteralPath $names.current).Hash
$sessionPath = $open.session
$processIdentity = $open.gameProcess
function Assert-LoadOwner {
    $current = Read-RunSession $Holder $Game $desktop $desktop $desktop ([bool]$RendererPreview)
    Assert-LoadMode $current $Game ([bool]$RendererPreview)
    if ($current.session -ne $sessionPath -or (Get-FileHash -LiteralPath $names.current).Hash -ne $sessionHash) {
        throw 'Session identity changed during load; no further requests allowed'
    }
    $processes = @(Get-Process MassEffect1, MassEffect2, MassEffect3 -ErrorAction SilentlyContinue)
    Assert-LoadProcessSet $processes $processIdentity $exe
    $still = Get-Process -Id $processIdentity.pid -ErrorAction SilentlyContinue
    if (-not $still) { throw 'Owned game process exited' }
    Assert-RunProcess $still $processIdentity $exe
}
Assert-LoadOwner
$profile = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'BioWare\Mass Effect Legendary Edition'
if (-not $open.profile -or [IO.Path]::GetFullPath($open.profile) -ine [IO.Path]::GetFullPath($profile)) { throw 'Save profile differs from the protected backup' }
$saves = Join-Path $profile "Save\$($names.folder)"
if (-not $Save) {
    $found = @(Get-ChildItem -LiteralPath $saves -Directory -Filter 'VR_*' | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'Save_0004.pcsav') })
    if ($found.Count -ne 1) { throw 'Expected one LE2 VR career with Save_0004.pcsav; supply -Save explicitly' }
    $Save = Join-Path $found[0].FullName 'Save_0004.pcsav'
} elseif (-not [IO.Path]::IsPathRooted($Save)) { $Save = Join-Path $saves $Save }
$Save = Resolve-RunChild $saves $Save
$file = Get-Item -LiteralPath $Save
if ($file.PSIsContainer -or $file.Directory.Parent.FullName -ine [IO.Path]::GetFullPath($saves)) { throw 'Save must be a file immediately under a career in the selected game save root' }
foreach ($path in @($saves, $file.Directory.FullName, $Save)) {
    if ((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked save paths are refused' }
}
$expected = Get-LoadDescriptor $file.Directory.Name $file.Name $Game
$hash = (Get-FileHash -LiteralPath $Save).Hash
$receiptDir = Resolve-RunChild $sessionPath ('load-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $receiptDir | Out-Null
$requests = New-Object Collections.Generic.List[object]
$artifacts = @($exe, (Join-Path $bin "ASI\${Game}MEVR.asi"), (Join-Path $bin "ASI\${Game}MEVR.ini"))
$modpath = Join-Path $bin "ASI\${Game}MEVR.modpath"
if (Test-Path -LiteralPath $modpath) {
    $artifacts += $modpath
    $module = [IO.File]::ReadAllText($modpath).Trim()
    if (-not [IO.Path]::IsPathRooted($module) -or -not (Test-Path -LiteralPath $module -PathType Leaf)) { throw 'Deployed modpath does not identify an existing module' }
    $artifacts += $module
}
if ($RendererPreview) { $artifacts += Join-Path $bin 'MELE3VR.ini' }
$artifactIdentity = @($artifacts | ForEach-Object { [pscustomobject]@{ path = $_; sha256 = (Get-FileHash -LiteralPath $_).Hash } })
$receipt = [ordered]@{ schemaVersion = 1; operation = 'normal-save-load'; game = $Game; holder = $Holder;
    rendererPreview = [bool]$RendererPreview; startedUtc = [DateTime]::UtcNow.ToString('o'); session = $sessionPath; sessionMetadataSha256 = $sessionHash;
    process = $processIdentity; artifacts = $artifactIdentity; save = $Save; saveSha256Before = $hash; expected = $expected;
    passed = $false; transitionObserved = $false; gameplayObserved = $false; descriptorMatched = $false;
    saveUnchanged = $false; artifactsUnchanged = $false; error = $null }
$console = Join-Path $PSScriptRoot '..\console.ps1'
function Ask([string]$Command) {
    Assert-LoadOwner  # immediately before every request, including polling
    if (Test-Path -LiteralPath (Join-Path $MevrDataDir 'console.request')) { throw 'A console request is already pending; refusing overlap' }
    $sent = [DateTime]::UtcNow
    $raw = (& powershell -NoProfile -ExecutionPolicy Bypass -File $console $Command -TimeoutSeconds 10 2>&1 | Out-String)
    $code = $LASTEXITCODE
    Assert-LoadOwner
    $requests.Add([pscustomobject]@{ sentUtc = $sent.ToString('o'); command = $Command; exitCode = $code; raw = $raw })
    if ($code -ne 0) { return $null }
    $result = Get-Item -LiteralPath (Join-Path $MevrDataDir 'console.result')
    if ($result.LastWriteTimeUtc -lt $sent) { throw 'Stale console reply timestamp' }
    return ConvertFrom-LoadReply $raw $Command
}
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $before = $null
    while ([DateTime]::UtcNow -lt $deadline -and -not $before) {
        $before = ConvertFrom-LoadState (Ask 'mevr state') $Game
        if (-not $before) { Start-Sleep -Seconds 1 }
    }
    if (-not $before) { throw 'No valid state response before load' }
    $receipt.before = $before
    $receipt.descriptorBefore = ConvertFrom-LoadDescriptor (Ask 'mevr savedesc')
    if ((Get-FileHash -LiteralPath $Save).Hash -ne $hash) { throw 'Save changed before queuing' }
    $command = "mevr load $($expected.career) $($expected.name) $($expected.index) $($expected.type)"
    $queued = Ask $command
    $receipt.queued = $queued
    if ($queued -notmatch '^mevr load: queued ') { throw 'Native load request was not acknowledged' }
    $transition = $false; $done = $false
    while ([DateTime]::UtcNow -lt $deadline -and -not $done) {
        Start-Sleep -Seconds 1
        $state = ConvertFrom-LoadState (Ask 'mevr state') $Game
        if (-not $state) { continue }
        $transition = $transition -or (Test-LoadTransition $before $state $Game)
        $receipt.after = $state
        $receipt.transitionObserved = $transition
        $receipt.gameplayObserved = Test-LoadGameplay $state
        if (-not $transition -or -not $receipt.gameplayObserved) { continue }
        $observed = ConvertFrom-LoadDescriptor (Ask 'mevr savedesc')
        $receipt.descriptorAfter = $observed
        $receipt.descriptorMatched = Test-LoadDescriptor $expected $observed
        if (-not $receipt.descriptorMatched) { continue }
        # Recheck gameplay after the descriptor request, not an earlier title/loading sample.
        $stable = ConvertFrom-LoadState (Ask 'mevr state') $Game
        $done = (Test-LoadGameplay $stable) -and -not (Test-LoadTransition $state $stable $Game)
        if ($stable) { $receipt.after = $stable }
    }
    if (-not $done) { throw 'No observed identity transition plus matching descriptor and stable gameplay within timeout' }
    Assert-LoadOwner
    $receipt.passed = $true
} catch {
    $receipt.error = $_.Exception.Message
} finally {
    $receipt.finishedUtc = [DateTime]::UtcNow.ToString('o')
    try {
        $receipt.saveSha256After = (Get-FileHash -LiteralPath $Save).Hash
        $receipt.saveUnchanged = $receipt.saveSha256After -eq $hash
        $receipt.artifactsUnchanged = @($artifactIdentity | Where-Object { (Get-FileHash -LiteralPath $_.path).Hash -ne $_.sha256 }).Count -eq 0
    } catch { $receipt.error = "Final hash verification failed: $($_.Exception.Message)" }
    if (-not $receipt.saveUnchanged -or -not $receipt.artifactsUnchanged) {
        $receipt.passed = $false
        if (-not $receipt.error) { $receipt.error = 'Save or runtime artifact changed during load' }
    }
    $receipt.requests = @($requests.ToArray())
    $receipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $receiptDir 'receipt.json') -Encoding utf8
}
"Private load receipt: $receiptDir\receipt.json"
if (-not $receipt.passed) { throw $receipt.error }
"$Game normal save loaded: $($file.Name); observed transition, exact descriptor, gameplay and unchanged source hash"
