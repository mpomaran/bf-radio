param(
    [Parameter(Mandatory = $true)][string]$OutputZip,
    [Parameter(Mandatory = $true)][string]$DigirigConfig,
    [Parameter(Mandatory = $true)][string]$Readme,
    [Parameter(Mandatory = $true)][string]$ConfigureCmd,
    [Parameter(Mandatory = $true)][string]$TxTextCmd,
    [Parameter(Mandatory = $true)][string]$RxChirpCmd,
    [Parameter(Mandatory = $true)][string[]]$Exe
)

$ErrorActionPreference = "Stop"

$OutputZip = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputZip)
$StageRoot = Join-Path ([System.IO.Path]::GetDirectoryName($OutputZip)) "bf-radio-field-kit.stage"
$PackageRoot = Join-Path $StageRoot "bf-radio-field-kit"
$BinDir = Join-Path $PackageRoot "bin"
$ConfigDir = Join-Path $PackageRoot "config"

$ExePaths = @()
foreach ($item in $Exe) {
    foreach ($part in ($item -split ",")) {
        $trimmed = $part.Trim()
        if ($trimmed) {
            $ExePaths += $trimmed
        }
    }
}

Remove-Item -Recurse -Force -Path $StageRoot -ErrorAction SilentlyContinue
Remove-Item -Force -Path $OutputZip -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
New-Item -ItemType Directory -Force -Path $ConfigDir | Out-Null

foreach ($path in $ExePaths) {
    Copy-Item -Force -Path $path -Destination $BinDir
}

Copy-Item -Force -Path $DigirigConfig -Destination (Join-Path $ConfigDir "digirig.cfg")
Copy-Item -Force -Path $Readme -Destination (Join-Path $PackageRoot "README.txt")
Copy-Item -Force -Path $ConfigureCmd -Destination (Join-Path $PackageRoot "configure.cmd")
Copy-Item -Force -Path $TxTextCmd -Destination (Join-Path $PackageRoot "tx_text.cmd")
Copy-Item -Force -Path $RxChirpCmd -Destination (Join-Path $PackageRoot "rx_chirp.cmd")

Compress-Archive -Path $PackageRoot -DestinationPath $OutputZip -Force
Remove-Item -Recurse -Force -Path $StageRoot

Write-Host "Portable ZIP ready: $OutputZip"
