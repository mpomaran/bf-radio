param(
    [string]$OutputZip = "dist\bf-radio-field-kit.zip",
    [string]$BazelSh = "C:\Program Files\Git\bin\bash.exe"
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

if (Test-Path $BazelSh) {
    $env:BAZEL_SH = $BazelSh
}

bazel build `
    //src:digirig_config `
    //src:bf_recorder `
    //src:bf_wav_tx `
    //src:bf_text_tx `
    //src:bf_chirp_rx `
    //lab:chirp_modem `
    //lab:pcm_to_wav `
    //lab:wav_to_pcm

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputZip) | Out-Null

& (Join-Path $RepoRoot "tools\make_portable_zip.ps1") `
    -OutputZip (Join-Path $RepoRoot $OutputZip) `
    -DigirigConfig (Join-Path $RepoRoot "tools\portable_digirig.cfg") `
    -Readme (Join-Path $RepoRoot "tools\portable_README.txt") `
    -ConfigureCmd (Join-Path $RepoRoot "tools\portable_configure.cmd") `
    -TxTextCmd (Join-Path $RepoRoot "tools\portable_tx_text.cmd") `
    -RxChirpCmd (Join-Path $RepoRoot "tools\portable_rx_chirp.cmd") `
    -Exe @(
        (Join-Path $RepoRoot "bazel-bin\src\digirig_config.exe"),
        (Join-Path $RepoRoot "bazel-bin\src\bf_recorder.exe"),
        (Join-Path $RepoRoot "bazel-bin\src\bf_wav_tx.exe"),
        (Join-Path $RepoRoot "bazel-bin\src\bf_text_tx.exe"),
        (Join-Path $RepoRoot "bazel-bin\src\bf_chirp_rx.exe"),
        (Join-Path $RepoRoot "bazel-bin\lab\chirp_modem.exe"),
        (Join-Path $RepoRoot "bazel-bin\lab\pcm_to_wav.exe"),
        (Join-Path $RepoRoot "bazel-bin\lab\wav_to_pcm.exe")
    )
