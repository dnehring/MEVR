# SPDX-License-Identifier: MIT
# Extracts an ME3Tweaks merge mod (.m3m) into a folder: the JSON manifest plus any embedded assets.
# Read-only on the input. Format (v1): "M3MM", version byte, Unreal string (JSON manifest),
# int32 asset count, then per asset: "MMV1", Unreal string name, int32 size, bytes.
# Unreal string = int32 length; negative means UTF-16 with -length chars, positive means ASCII;
# the length includes the null terminator.
param(
    [Parameter(Mandatory)] [string]$Path,
    [Parameter(Mandatory)] [string]$OutDir
)

$ErrorActionPreference = 'Stop'

function Read-UnrealString([IO.BinaryReader]$r) {
    $len = $r.ReadInt32()
    if ($len -lt 0) {
        $bytes = $r.ReadBytes(-$len * 2)
        return [Text.Encoding]::Unicode.GetString($bytes).TrimEnd([char]0)
    }
    $bytes = $r.ReadBytes($len)
    return [Text.Encoding]::ASCII.GetString($bytes).TrimEnd([char]0)
}

$fs = [IO.File]::OpenRead((Resolve-Path $Path))
$r = New-Object IO.BinaryReader($fs)
try {
    $magic = [Text.Encoding]::ASCII.GetString($r.ReadBytes(4))
    if ($magic -ne 'M3MM') { throw "Not an .m3m file (magic '$magic')" }
    $version = $r.ReadByte()
    if ($version -ne 1) { throw "Unsupported .m3m version $version" }

    New-Item -ItemType Directory -Force $OutDir | Out-Null
    $manifest = Read-UnrealString $r
    $json = $manifest | ConvertFrom-Json
    Set-Content -Path (Join-Path $OutDir 'manifest.json') -Value ($json | ConvertTo-Json -Depth 50) -Encoding utf8

    # Write inline UnrealScript out as .uc files: scriptupdate replaces one function,
    # addtoclassorreplace adds or replaces class members.
    $scriptDir = Join-Path $OutDir 'scripts'
    $scriptCount = 0
    foreach ($file in $json.files) {
        foreach ($change in $file.changes) {
            $pairs = @()
            if ($change.scriptupdate) {
                $pairs += ,@($change.scriptupdate.scriptfilename, $change.scriptupdate.scripttext)
            }
            if ($change.addtoclassorreplace) {
                $names = @($change.addtoclassorreplace.scriptfilenames)
                $texts = @($change.addtoclassorreplace.scripts)
                for ($k = 0; $k -lt $texts.Count; $k++) {
                    $n = if ($k -lt $names.Count -and $names[$k]) { $names[$k] } else { "$($change.entryname)_$k.uc" }
                    $pairs += ,@($n, $texts[$k])
                }
            }
            foreach ($p in $pairs) {
                New-Item -ItemType Directory -Force $scriptDir | Out-Null
                $header = "// $($file.filename) :: $($change.entryname)`r`n"
                Set-Content -Path (Join-Path $scriptDir ([IO.Path]::GetFileName($p[0]))) -Value ($header + $p[1]) -Encoding utf8
                $scriptCount++
            }
        }
    }

    $assets = @()
    if ($fs.Position -lt $fs.Length) {
        $count = $r.ReadInt32()
        for ($i = 0; $i -lt $count; $i++) {
            # Each embedded asset is prefixed with the marker "MMV1".
            $marker = [Text.Encoding]::ASCII.GetString($r.ReadBytes(4))
            if ($marker -ne 'MMV1') { throw "Unexpected asset marker '$marker' at asset $i" }
            $name = Read-UnrealString $r
            $size = $r.ReadInt32()
            $data = $r.ReadBytes($size)
            if ($data.Length -ne $size) { throw "Truncated asset '$name'" }
            $safe = [IO.Path]::GetFileName($name)
            [IO.File]::WriteAllBytes((Join-Path $OutDir $safe), $data)
            $assets += "$safe ($size bytes)"
        }
    }
    if ($fs.Position -ne $fs.Length) { throw "Parse ended at $($fs.Position) of $($fs.Length) bytes; format mismatch" }

    Write-Output "$([IO.Path]::GetFileName($Path)): manifest $($manifest.Length) chars, $scriptCount script(s), $($assets.Count) asset(s)"
    $assets | ForEach-Object { "  $_" }
} finally {
    $r.Dispose()
}
