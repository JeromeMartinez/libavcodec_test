##  Copyright (c) MediaArea.net SARL. All Rights Reserved.

Param([String]$arch = "x64")

$ErrorActionPreference = "Stop"

function Assert-Success([String]$step) {
    if ($LASTEXITCODE -ne 0) {
        throw "${step} failed (exit code ${LASTEXITCODE})"
    }
}

#-----------------------------------------------------------------------
# Setup
$release_directory = $PSScriptRoot
$root_directory = "${release_directory}\.."
$version = (Get-Content "${release_directory}\version.txt" -Raw).Trim()

$ffmpeg_bin_directory = "${root_directory}\ffmpeg\bin"
$ffmpeg_license = "${root_directory}\ffmpeg\LICENSE.txt"
$ffmpeg_howtogpu = "${root_directory}\ffmpeg\HowToGPU.txt"

#-----------------------------------------------------------------------
# Cleanup
$artifact = "${release_directory}\libavcodec-test_${version}_Windows_${arch}"
if (Test-Path "${artifact}") {
    Remove-Item -Force -Recurse "${artifact}"
}

$artifact = "${release_directory}\libavcodec-test_${version}_Windows_${arch}.zip"
if (Test-Path "${artifact}") {
    Remove-Item -Force "${artifact}"
}

#-----------------------------------------------------------------------
# Package
Push-Location "${release_directory}"
    New-Item -Force -ItemType Directory -Path "libavcodec-test_${version}_Windows_${arch}"
    Push-Location "libavcodec-test_${version}_Windows_${arch}"
        ### Copying: Exe ###
        Copy-Item -Force "${root_directory}\${arch}\Release\dec_tx_test.exe" .
        ### Copying: DLLs ###
        Get-ChildItem "${ffmpeg_bin_directory}\av*.dll", "${ffmpeg_bin_directory}\sw*.dll" | Copy-Item -Force
        ### Copying: Information files ###
        Copy-Item -Force "${ffmpeg_license}" .
        Copy-Item -Force "${ffmpeg_howtogpu}" .
        Copy-Item -Force "${root_directory}\README.md" .
        ### Archive
        & 7za.exe a -r -tzip -mx9 "..\libavcodec-test_${version}_Windows_${arch}.zip" *
        Assert-Success "Archive"
    Pop-Location
Pop-Location
