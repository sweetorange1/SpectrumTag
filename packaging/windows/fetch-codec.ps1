param(
    [string]$RuntimeDir,
    [string]$DownloadUrl = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip',
    [string]$LicenseUrl = 'https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/COPYING.GPLv3',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

# Populates the SPECTRUMTAG_STANDALONE_RUNTIME_DIR folder with a complete
# official GPL FFmpeg build plus its license text and a provenance notice.
# CMakeLists.txt embeds these three files as PE resources into SpectrumTag.exe.

if (-not $RuntimeDir) {
    $root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    $RuntimeDir = Join-Path $root 'packaging\windows\runtime'
}

function Get-RemoteFile([string]$url, [string]$target) {
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -L --fail --silent --show-error -o $target $url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $url" }
    } else {
        Invoke-WebRequest -Uri $url -OutFile $target -UseBasicParsing
    }
}

$RuntimeDir = [IO.Path]::GetFullPath($RuntimeDir)
New-Item -ItemType Directory -Path $RuntimeDir -Force | Out-Null
Write-Host "[1/5] Runtime directory: $RuntimeDir"

$zip = Join-Path $env:TEMP 'ffmpeg-win64-gpl.zip'
if ($Force -or -not (Test-Path -LiteralPath $zip)) {
    Write-Host "[2/5] Downloading $DownloadUrl"
    Get-RemoteFile $DownloadUrl $zip
} else {
    Write-Host "[2/5] Reusing cached archive: $zip"
}

$extract = Join-Path $env:TEMP 'ffmpeg-win64-gpl'
if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
Write-Host "[3/5] Extracting"
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($zip, $extract)

$sourceExe = Get-ChildItem -LiteralPath $extract -Filter 'ffmpeg.exe' -Recurse |
    Where-Object { $_.Directory.Name -eq 'bin' } |
    Select-Object -First 1
if (-not $sourceExe) { throw 'ffmpeg.exe not found inside the downloaded archive.' }
$targetExe = Join-Path $RuntimeDir 'ffmpeg.exe'
Copy-Item -LiteralPath $sourceExe.FullName -Destination $targetExe -Force

Write-Host "[4/5] Fetching license text"
Get-RemoteFile $LicenseUrl (Join-Path $RuntimeDir 'COPYING.GPLv3')

Write-Host "[5/5] Verifying codec"
$version = (& $targetExe -hide_banner -version | Select-Object -First 1)
$configuration = (& $targetExe -hide_banner -version | Select-String -Pattern '^configuration:' | Select-Object -First 1).Line
$sha256 = (Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash.ToLowerInvariant()

$encoders = & $targetExe -hide_banner -encoders 2>&1 | Out-String
$required = 'libmp3lame', 'libvorbis', 'libopus', 'libx264', 'libx265', 'libvpx-vp9', 'prores_ks', 'flac', 'pcm_s24le'
$missing = @()
foreach ($name in $required) {
    if ($encoders -notmatch ("(?m)^\s*[VAS]\S*\s+$([regex]::Escape($name))\s")) { $missing += $name }
}
if ($missing.Count) { throw "This build lacks required encoders: $($missing -join ', ')" }

$notice = @"
FFmpeg — third-party codec bundled with SpectrumTag (Windows standalone)

Component : ffmpeg.exe
Version   : $version
Build     : official full GPL build from BtbN (https://github.com/BtbN/FFmpeg-Builds),
            listed on https://ffmpeg.org/download.html
Download  : $DownloadUrl
SHA-256   : $sha256
License   : GNU General Public License version 3 (see COPYING.GPLv3)
Source    : https://github.com/FFmpeg/FFmpeg and the BtbN build scripts at
            https://github.com/BtbN/FFmpeg-Builds
Buildopts : $configuration

FFmpeg is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. FFmpeg is distributed WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

This binary is embedded as a PE resource inside SpectrumTag.exe and is
extracted, SHA-256 verified and run as a separate process for media import and
export. It is not linked into SpectrumTag. The complete corresponding source
code is available from the URLs above.
"@
$noticePath = Join-Path $RuntimeDir 'FFmpeg-NOTICE.txt'
[IO.File]::WriteAllText($noticePath, ($notice -replace "`r?`n", "`r`n"), (New-Object Text.UTF8Encoding $false))

Get-ChildItem -LiteralPath $RuntimeDir | Select-Object Name, Length
Write-Host ''
Write-Host "Version : $version"
Write-Host "SHA-256 : $sha256"
Write-Host "Runtime : $RuntimeDir"
Write-Host 'CODEC_READY'
