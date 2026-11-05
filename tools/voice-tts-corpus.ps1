# SPDX-License-Identifier: MIT
# A spoken test corpus without a person (research/54 step 2): Windows' own speech synthesis (System.Speech, every
# installed voice) reads the squad phrasings the parser's corpus holds (tests/SquadPhrasings.cpp, its Pass lines) into
# 16 kHz mono WAVs, one per line and voice, with lines.tsv naming each file's text. The recognizer's bench
# (mevr-voice-bench) and the in-game file feed (`mevr voice wav <file>`) take them. Synthetic speech is clean and
# evenly paced: a lower bound on the real microphone's errors, not a measurement of them.
#   tools/voice-tts-corpus.ps1 [-Out scratch\voice-corpus] [-Max 40] [-Rate 0] [-Voices "Zira,David"] [-Lines file.txt]
[CmdletBinding()]
param(
    [string] $Out = (Join-Path $PSScriptRoot '..\scratch\voice-corpus'),
    [int] $Max = 40,
    [int] $Rate = 0,
    [string] $Voices = '',
    [string] $Lines = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
New-Item -ItemType Directory -Force $Out | Out-Null

$texts = @()
if ($Lines) {
    $texts = Get-Content $Lines | Where-Object { $_.Trim() -ne '' }
} else {
    $source = Join-Path $PSScriptRoot '..\tests\SquadPhrasings.cpp'
    foreach ($m in [regex]::Matches((Get-Content $source -Raw), '\{Kind::Pass,\s*"((?:[^"\\]|\\.)*)"')) {
        $texts += $m.Groups[1].Value -replace '\\"', '"'
    }
}
if ($texts.Count -gt $Max) { $texts = $texts[0..($Max - 1)] }

$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$installed = $synth.GetInstalledVoices() | Where-Object { $_.Enabled } | ForEach-Object { $_.VoiceInfo.Name }
$use = if ($Voices) { $installed | Where-Object { $n = $_; ($Voices -split ',') | Where-Object { $n -like "*$_*" } } } else { $installed }
if (-not $use) { throw "no installed voice matches '$Voices' (installed: $($installed -join ', '))" }
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$tsv = @()
$n = 0
foreach ($voice in $use) {
    $synth.SelectVoice($voice)
    $synth.Rate = $Rate
    $short = ($voice -replace 'Microsoft ', '' -replace ' Desktop', '' -replace '[^A-Za-z0-9]', '')
    $i = 0
    foreach ($text in $texts) {
        $file = Join-Path $Out ('{0}-{1:D3}.wav' -f $short, $i)
        $synth.SetOutputToWaveFile($file, $format)
        $synth.Speak($text)
        $synth.SetOutputToNull()
        $tsv += "$([IO.Path]::GetFileName($file))`t$text"
        $i++; $n++
    }
}
$synth.Dispose()
Set-Content -Path (Join-Path $Out 'lines.tsv') -Value $tsv -Encoding utf8
Write-Output "$n files from $($texts.Count) lines and $($use.Count) voice(s) ($($use -join ', ')) in $Out"
