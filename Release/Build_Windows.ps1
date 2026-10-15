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
$ffmpeg_directory = "${root_directory}\ffmpeg"


$ffmpeg_url = "https://mediaarea.net/download/snapshots/binary/ffmpeg/latest"
$ffmpeg_dev_archive = "FFmpeg_Dev_Latest_Windows_Shared_${arch}.zip"
$ffmpeg_bin_archive = "FFmpeg_Bin_Latest_Windows_Shared_${arch}.zip"

#-----------------------------------------------------------------------
# Cleanup
if (Test-Path "${ffmpeg_directory}") {
    Remove-Item -Force -Recurse "${ffmpeg_directory}"
}

if (Test-Path "${root_directory}\${arch}") {
    Remove-Item -Force -Recurse "${root_directory}\${arch}"
}

#-----------------------------------------------------------------------
# Get FFmpeg
Push-Location "${root_directory}"
    curl.exe -LO "${ffmpeg_url}/${ffmpeg_dev_archive}"
    Assert-Success "download (${ffmpeg_dev_archive})"

    curl.exe -LO "${ffmpeg_url}/${ffmpeg_bin_archive}"
    Assert-Success "download (${ffmpeg_bin_archive})"

    7z.exe x -y -o"${ffmpeg_directory}" "${ffmpeg_dev_archive}"
    Assert-Success "extract (${ffmpeg_dev_archive})"

    7z.exe x -y -o"${ffmpeg_directory}" "${ffmpeg_bin_archive}"
    Assert-Success "extract (${ffmpeg_bin_archive})"

    Remove-Item -Force "${ffmpeg_dev_archive}", "${ffmpeg_bin_archive}"
Pop-Location

#-----------------------------------------------------------------------
# Build
Push-Location "${root_directory}"
    MSBuild "dec_tx_test.vcxproj" -p:"Configuration=Release;Platform=${arch};PlatformToolset=v143"
    Assert-Success "MSBuild"
Pop-Location
