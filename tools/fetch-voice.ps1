# SPDX-License-Identifier: MIT
# Fetches what the voice orders need and the repository doesn't carry (research/54): Moonshine Voice's prebuilt Windows
# library (MIT; the static libraries, ONNX Runtime and the C API header) into external/moonshine-voice, and the streaming
# English models (MIT) into models/voice/<size>-streaming-en. Both folders are gitignored; CMake builds MEVR-Voice.dll and
# mevr-voice-bench only when the library is there. Later: llama.cpp's CPU build and the Qwen3-0.6B GGUF for the model tier
# (-Llm).
#   tools/fetch-voice.ps1 [-Models tiny,small,medium] [-Llm]
[CmdletBinding()]
param(
    [string[]] $Models = @('tiny', 'small', 'medium'),
    [switch] $Llm,
    [string] $MoonshineRelease = 'v0.1.5',
    [string] $LlamaRelease = 'b12128'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$external = Join-Path $root 'external\moonshine-voice'
$modelRoot = Join-Path $root 'models\voice'  # not $models: PowerShell's variables are case-insensitive ($Models is the parameter)

function Fetch([string] $uri, [string] $out) {
    if (Test-Path $out) { return }
    $dir = Split-Path $out -Parent
    New-Item -ItemType Directory -Force $dir | Out-Null
    Write-Output "  $uri"
    Invoke-WebRequest -Uri $uri -OutFile "$out.part"
    Move-Item "$out.part" $out -Force
}

if (-not (Test-Path (Join-Path $external 'lib\moonshine.lib'))) {
    Write-Output "Moonshine Voice $MoonshineRelease (MIT) -> $external"
    $tar = Join-Path $env:TEMP "moonshine-voice-windows-x86_64-$MoonshineRelease.tar.gz"
    Fetch "https://github.com/moonshine-ai/moonshine/releases/download/$MoonshineRelease/moonshine-voice-windows-x86_64.tar.gz" $tar
    $unpack = Join-Path $env:TEMP "moonshine-voice-$MoonshineRelease"
    if (Test-Path $unpack) { Remove-Item $unpack -Recurse -Force -Confirm:$false }
    New-Item -ItemType Directory -Force $unpack | Out-Null
    tar -xzf $tar -C $unpack
    if ($LASTEXITCODE -ne 0) { throw "tar failed on $tar" }
    New-Item -ItemType Directory -Force $external | Out-Null
    Copy-Item (Join-Path $unpack 'moonshine-voice-windows-x86_64\include') $external -Recurse -Force
    Copy-Item (Join-Path $unpack 'moonshine-voice-windows-x86_64\lib') $external -Recurse -Force
    Fetch 'https://raw.githubusercontent.com/moonshine-ai/moonshine/main/LICENSE' (Join-Path $external 'LICENSE')
} else {
    Write-Output "Moonshine Voice is in $external"
}

$files = 'streaming_config.json', 'frontend.model.ort', 'frontend.weights.ort', 'encoder.ort', 'adapter.ort', 'cross_kv.ort', 'decoder_kv.ort', 'tokenizer.bin'
foreach ($size in $Models) {
    $dir = Join-Path $modelRoot "$size-streaming-en"
    Write-Output "Moonshine $size streaming English (MIT) -> $dir"
    foreach ($f in $files) {
        Fetch "https://huggingface.co/moonshine-ai/moonshine-voice-assets/resolve/main/model/$size-streaming-en/quantized_26_08_21/$f" (Join-Path $dir $f)
    }
}

if ($Llm) {
    $llama = Join-Path $root 'external\llama-cpp'
    Write-Output "llama.cpp $LlamaRelease CPU build (MIT) -> $llama"
    $zip = Join-Path $env:TEMP "llama-$LlamaRelease-bin-win-cpu-x64.zip"
    Fetch "https://github.com/ggml-org/llama.cpp/releases/download/$LlamaRelease/llama-$LlamaRelease-bin-win-cpu-x64.zip" $zip
    New-Item -ItemType Directory -Force $llama | Out-Null
    Expand-Archive -Path $zip -DestinationPath $llama -Force
    $src = Join-Path $env:TEMP "llama-$LlamaRelease-src.zip"
    Fetch "https://github.com/ggml-org/llama.cpp/archive/refs/tags/$LlamaRelease.zip" $src
    $srcDir = Join-Path $env:TEMP "llama-src-$LlamaRelease"
    if (-not (Test-Path $srcDir)) { Expand-Archive -Path $src -DestinationPath $srcDir -Force }
    $inc = Join-Path $llama 'include'
    New-Item -ItemType Directory -Force $inc | Out-Null
    $top = Get-ChildItem $srcDir | Select-Object -First 1
    Copy-Item (Join-Path $top.FullName 'include\llama.h') $inc -Force
    Copy-Item (Join-Path $top.FullName 'ggml\include\*.h') $inc -Force
    Copy-Item (Join-Path $top.FullName 'LICENSE') (Join-Path $llama 'LICENSE') -Force
    $gguf = Join-Path $modelRoot 'llm'
    Write-Output "Qwen3-0.6B GGUF (Apache-2.0) -> $gguf"
    Fetch 'https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/Qwen3-0.6B-Q8_0.gguf' (Join-Path $gguf 'Qwen3-0.6B-Q8_0.gguf')
}
Write-Output 'Done. Configure CMake again (it finds external/moonshine-voice) and build MEVR-Voice and mevr-voice-bench.'
