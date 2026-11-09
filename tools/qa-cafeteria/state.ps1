# SPDX-License-Identifier: MIT
# File-only backup/restore operations, also exercised against temporary fixtures by test-offline.ps1.
$ErrorActionPreference='Stop'
function Save-QaBackup([string]$RunDirectory,[string[]]$Paths,[string[]]$Trees,[string]$Binary,[string]$Runtime) {
    $manifest=Join-Path $RunDirectory 'before.json'
    if(Test-Path -LiteralPath $manifest){throw 'Backup already exists; refusing to replace it.'}
    foreach($tree in $Trees){$Paths+=@(Get-ChildItem -LiteralPath $tree -Recurse -File).FullName}
    New-Item -ItemType Directory -Path (Join-Path $RunDirectory 'before')|Out-Null
    $files=@()
    foreach($path in $Paths|Select-Object -Unique){
        $exists=Test-Path -LiteralPath $path
        $saved=Join-Path $RunDirectory ('before/'+$files.Count)
        $sha=$null
        if($exists){
            $sha=(Get-FileHash -LiteralPath $path).Hash
            Copy-Item -LiteralPath $path -Destination $saved
            if((Get-FileHash -LiteralPath $saved).Hash -ne $sha){throw "Backup mismatch: $path"}
        }
        $files+=@{path=$path;saved=$saved;exists=$exists;sha256=$sha}
    }
    @{created=(Get-Date).ToString('o');files=$files;trees=$Trees;binary=$Binary;runtime=$Runtime}|
        ConvertTo-Json -Depth 6|Set-Content -LiteralPath $manifest
}
function Test-QaBackup([object]$Manifest) {
    @(foreach($f in $Manifest.files){
        if($f.exists){
            if(-not (Test-Path -LiteralPath $f.path) -or (Get-FileHash -LiteralPath $f.path).Hash -ne $f.sha256){$f.path}
        }elseif(Test-Path -LiteralPath $f.path){$f.path}
    })
}
function Restore-QaBackup([string]$RunDirectory) {
    $m=Get-Content -LiteralPath (Join-Path $RunDirectory 'before.json') -Raw|ConvertFrom-Json
    # Check every backup before changing any live file.
    foreach($f in $m.files){
        if($f.exists -and (-not (Test-Path -LiteralPath $f.saved) -or (Get-FileHash -LiteralPath $f.saved).Hash -ne $f.sha256)){throw "Backup damaged: $($f.saved)"}
    }
    $archive=Join-Path $RunDirectory ('after-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $archive|Out-Null
    $known=@{};$changed=@()
    foreach($f in $m.files){
        $known[$f.path]=$true
        $exists=Test-Path -LiteralPath $f.path
        if($exists -and $f.exists -and (Get-FileHash -LiteralPath $f.path).Hash -eq $f.sha256){continue}
        if(-not $exists -and -not $f.exists){continue}
        if($exists){Copy-Item -LiteralPath $f.path -Destination (Join-Path $archive ([string]$changed.Count))}
        $changed+=@($f.path)
        if($f.exists){Copy-Item -LiteralPath $f.saved -Destination $f.path -Force}
        elseif($exists){Remove-Item -LiteralPath $f.path}
    }
    foreach($tree in $m.trees){
        $root=[IO.Path]::GetFullPath($tree).TrimEnd('\')+'\'
        foreach($file in Get-ChildItem -LiteralPath $tree -Recurse -File){
            if($known.ContainsKey($file.FullName)){continue}
            $target=[IO.Path]::GetFullPath($file.FullName)
            if(-not $target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw 'Generated file is outside protected tree.'}
            Move-Item -LiteralPath $target -Destination (Join-Path $archive ('new-'+$changed.Count))
            $changed+=@($target)
        }
    }
    $mismatches=@(Test-QaBackup $m)
    if($mismatches.Count){throw "Restoration mismatch: $($mismatches -join ', ')"}
    @{restored=(Get-Date).ToString('o');verifiedFiles=$m.files.Count;changed=$changed;archive=$archive;binary=$m.binary;runtime=$m.runtime}|
        ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $RunDirectory 'restored.json')
    "Restored and verified $($m.files.Count) files; $($changed.Count) test changes archived."
}
