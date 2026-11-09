# SPDX-License-Identifier: MIT
# Private, frozen monolithic QA build. The process-only runtime shim never enters production source.
#Requires -Version 7.0
param([Parameter(Mandatory)][string]$StageDirectory,
      [Parameter(Mandatory)][string]$SteamVRManifest)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage=[IO.Path]::GetFullPath($StageDirectory)
$privateRoot=[IO.Path]::GetFullPath((Join-Path $repo 'scratch'))+'\'
if(-not $stage.StartsWith($privateRoot,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $stage)){
    throw 'Use a new directory under this checkout/scratch.'
}
if(-not (Test-Path -LiteralPath $SteamVRManifest)){throw 'SteamVR manifest is missing.'}
New-Item -ItemType Directory -Path $stage|Out-Null
Start-Transcript -LiteralPath "$stage\build-transcript.txt"|Out-Null
try{
    $source=Join-Path $stage 'source'
    New-Item -ItemType Directory -Path $source|Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Destination $source
    foreach($name in 'src','assets','tools','tests','docs'){
        Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $source -Recurse
    }
    # Dependencies stay in their checkout; generated build output belongs to this private build directory.
    foreach($name in 'external','third_party','reference'){
        New-Item -ItemType Junction -Path (Join-Path $source $name) -Target (Join-Path $repo $name)|Out-Null
    }
    $file=Join-Path $source 'src\plugin\Halcyon.cpp'
    $original=[IO.File]::ReadAllText($file)
    if($original.Contains('qa-replay-jump.use-steamxr')){throw 'Source already contains a private runtime override.'}
    $manifest=[IO.Path]::GetFullPath($SteamVRManifest).Replace('\','/')
    if($manifest.Contains('"')){throw 'Invalid manifest path.'}
    $shim=@'
    // PRIVATE QA ONLY: select SteamVR for this process; leave the system OpenXR runtime alone.
    wchar_t module[32768]{};
    HMODULE qaSelf = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Attach), &qaSelf) && GetModuleFileNameW(qaSelf, module, 32768) != 0) {
        std::wstring marker(module);
        marker = marker.substr(0, marker.find_last_of(L"\\/") + 1) + L"qa-replay-jump.use-steamxr";
        if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
            SetEnvironmentVariableW(L"XR_RUNTIME_JSON", L"@MANIFEST@");
            log::Info("QA camera diagnostic: process-only SteamVR runtime selected");
        }
    }
'@
    if(([regex]::Matches($original,'void Attach\(\) \{')).Count -ne 1){throw 'Attach anchor changed.'}
    [IO.File]::WriteAllText($file,$original.Replace('void Attach() {',"void Attach() {`n"+$shim.Replace('@MANIFEST@',$manifest)))
    $files=@(Get-ChildItem -LiteralPath "$source\src" -Recurse -File)
    @($files|ForEach-Object {@{path=[IO.Path]::GetRelativePath($source,$_.FullName);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})|
        ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$stage\source-hashes.json"
    cmake -S $source -B "$stage\build" -G 'Visual Studio 17 2022' -A x64 -DMEVR_HOT_RELOAD=OFF -DMEVR_LE1_LESDK=OFF -DMEVR_VOICE=OFF -DMEVR_BUILD_TESTS=ON
    if($LASTEXITCODE){throw 'Configure failed.'}
    cmake --build "$stage\build" --config Release --target MEVR-LE1 mevr-tests -- /m:4 /nodeReuse:false /p:TrackFileAccess=false
    if($LASTEXITCODE){throw 'Candidate build failed.'}
    & "$stage\build\Release\mevr-tests.exe"
    if($LASTEXITCODE){throw 'Candidate tests failed.'}
    $binary=Join-Path $stage 'build\Release\LE1MEVR.asi'
    $hash=(Get-FileHash -LiteralPath $binary).Hash
    # Restrict the private copy of the old marker runner to THIS reviewed binary. Public runner remains unchanged.
    $runner=Join-Path $source 'tools\qa-cafeteria\run.ps1'
    $text=[IO.File]::ReadAllText($runner)
    $known='CE247578A6672A7786B024010EDEC1B11725791EEB8FF65F7A96D939EC89F216'
    if(-not $text.Contains($known)){throw 'QA marker runner anchor changed.'}
    [IO.File]::WriteAllText($runner,$text.Replace($known,$hash))
    @{binary=$binary;sha256=$hash;source=$source;runtimeManifest=$manifest;privateQaOnly=$true;created=(Get-Date).ToString('o')}|
        ConvertTo-Json|Set-Content -LiteralPath "$stage\candidate.json"
}finally{Stop-Transcript|Out-Null}
