#!/bin/bash

set -e # fail on any error

#-----------------------------------------------------------------------
# Setup
release_directory="$(readlink -f "$(dirname "${BASH_SOURCE}")")"
root_directory="$(readlink -f "${release_directory}/..")"
ffmpeg_directory="${root_directory}/ffmpeg"

version="$(<"${release_directory}/version.txt")"
package_name="libavcodec-test_${version}_Mac_universal"

build_type="Shared"

macos_version_min="10.15"
archs="x86_64 arm64"

#-----------------------------------------------------------------------
# Cleanup
rm -f "${release_directory}/${package_name}.zip"
rm -fr "${release_directory}/${package_name}"
rm -fr "${ffmpeg_directory}"

#-----------------------------------------------------------------------
# Get FFmpeg
pushd "${root_directory}"
    for package in "Dev" "Bin" ; do
        archive="FFmpeg_${package}_Latest_Mac_${build_type}_universal.zip"
        curl -LO "https://mediaarea.net/download/snapshots/binary/ffmpeg/latest/${archive}"
        unzip -oq "${archive}" -d "${ffmpeg_directory}"
        rm -f "${archive}"
    done
popd

#-----------------------------------------------------------------------
# Build
pushd "${root_directory}"
    cflags="-mmacosx-version-min=${macos_version_min} -I${ffmpeg_directory}/include"
    ldflags="-mmacosx-version-min=${macos_version_min} -L${ffmpeg_directory}/lib"
    for arch in ${archs} ; do
        cflags="${cflags} -arch ${arch}"
        ldflags="${ldflags} -arch ${arch}"
    done

    make CFLAGS="${cflags}" LDFLAGS="${ldflags}"
popd

#-----------------------------------------------------------------------
# Package
pushd "${release_directory}"
    mkdir -p "${package_name}/bin" "${package_name}/lib"

    cp "${root_directory}/dec_tx_test" "${package_name}/bin/"
    cp "${ffmpeg_directory}/lib/"*.dylib "${package_name}/lib/"
    cp "${root_directory}/README.md" "${package_name}/"
    cp "${ffmpeg_directory}/LICENSE.txt" "${package_name}/"
    cp "${ffmpeg_directory}/HowToGPU.txt" "${package_name}/"

    libraries=""
    binaries="${package_name}/bin/dec_tx_test"


    for bin in "${package_name}/bin/dec_tx_test" ; do
        if [ ! -f "${bin}" ] ; then
            continue
        fi
        for lib in $(otool -L "${bin}" | grep "${ffmpeg_directory}/lib" | awk -F' ' '{print $1 }') ; do
            install_name_tool -change "${lib}" "@executable_path/../lib/$(basename ${lib})" "${bin}"
        done
    done
    for lib in "${package_name}/lib/"*.dylib ; do
        if [ ! -f "${lib}" ] ; then
            continue
        fi
        install_name_tool -id "@executable_path/../lib/$(basename ${lib})" "${lib}"
    done

    pushd "${package_name}"
        zip -r "../${package_name}.zip" *
    popd
popd
