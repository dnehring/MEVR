# SPDX-License-Identifier: MIT
# The fine-tune's environment (tools/voice-model/README.md): a Python 3.12 venv at .venv-voice (gitignored) with torch
# (CUDA 12.4 wheels), transformers, peft, datasets, accelerate and the gguf package; and llama.cpp's convert script
# (convert_hf_to_gguf.py with its gguf-py) from the release's source zip, into external/llama-cpp/convert. Idempotent.
#   tools/voice-model/setup-env.ps1 [-Python "C:\...\python.exe"] [-LlamaRelease b12128]
[CmdletBinding()]
param(
    [string] $Python = '',
    [string] $LlamaRelease = 'b12128'
)
# 'Continue', not 'Stop': Windows PowerShell 5.1 turns pip's progress on stderr into a terminating error (CLAUDE.md
# "Gotchas"); the exit codes are checked instead.
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$venv = Join-Path $root '.venv-voice'
if (-not $Python) {
    $Python = (& py -3.12 -c "import sys; print(sys.executable)" 2>$null)
    if (-not $Python) { throw 'no Python 3.12 (py -3.12): install it or pass -Python' }
}
if (-not (Test-Path (Join-Path $venv 'Scripts\python.exe'))) {
    Write-Output "venv: $venv from $Python"
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'venv failed' }
}
$pip = Join-Path $venv 'Scripts\python.exe'
& $pip -m pip install --upgrade pip --quiet
& $pip -m pip install --quiet torch --index-url https://download.pytorch.org/whl/cu124
if ($LASTEXITCODE -ne 0) { throw 'torch install failed' }
& $pip -m pip install --quiet "transformers>=4.51" peft datasets accelerate gguf sentencepiece protobuf
if ($LASTEXITCODE -ne 0) { throw 'pip install failed' }
& $pip -c "import torch, transformers, peft; print('torch', torch.__version__, 'cuda', torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else '-', '| transformers', transformers.__version__, '| peft', peft.__version__)"

# llama.cpp's convert script and its gguf-py, from the tagged source (tools/fetch-voice.ps1 -Llm leaves the zip in TEMP).
$convert = Join-Path $root 'external\llama-cpp\convert'
if (-not (Test-Path (Join-Path $convert 'convert_hf_to_gguf.py'))) {
    $src = Join-Path $env:TEMP "llama-$LlamaRelease-src.zip"
    if (-not (Test-Path $src)) {
        Invoke-WebRequest -Uri "https://github.com/ggml-org/llama.cpp/archive/refs/tags/$LlamaRelease.zip" -OutFile $src
    }
    $srcDir = Join-Path $env:TEMP "llama-src-$LlamaRelease"
    if (-not (Test-Path $srcDir)) { Expand-Archive -Path $src -DestinationPath $srcDir -Force }
    $top = Get-ChildItem $srcDir -Directory | Select-Object -First 1
    New-Item -ItemType Directory -Force $convert | Out-Null
    Copy-Item (Join-Path $top.FullName 'convert_hf_to_gguf.py') $convert -Force
    Copy-Item (Join-Path $top.FullName 'gguf-py') (Join-Path $convert 'gguf-py') -Recurse -Force
    Write-Output "convert script: $convert"
}
Write-Output "Done: $venv"
