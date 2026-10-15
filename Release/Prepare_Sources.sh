#!/bin/bash

##  Copyright (c) MediaArea.net SARL. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license that can
##  be found in the License.html file in the root of the source tree.
##

set -e # fail on any error

#-----------------------------------------------------------------------
# Setup
release_directory="$(readlink -f "$(dirname "${BASH_SOURCE}")")"
root_directory="$(readlink -f "${release_directory}/..")"
sources="libavcodec-test"
version="${1}"

if [[ -e "${version}" ]] ; then
    echo "usage: ${0} <version>"
    exit 1
fi

#-----------------------------------------------------------------------
# Cleaning
pushd "${release_directory}"
    rm -f libavcodec-test_${version}.tar.xz
    rm -f libavcodec-test_${version}.7z
popd

#-----------------------------------------------------------------------
# Upgrade version
pushd "${release_directory}"
    echo "${version}" > version.txt
popd

#-----------------------------------------------------------------------
# Upgrade OBS project
pushd "${release_directory}"
    sed -i "s|%VERSION%|${version}|g" OBS/libavcodec-test.dsc
    sed -i "s|%VERSION%|${version}|g" OBS/libavcodec-test.spec
    sed -i "s|%VERSION%|${version}|g" OBS/PKGBUILD
popd

#-----------------------------------------------------------------------
# Upgrade changelogs
date_rfc2822="$(LC_ALL=C date -u -R)"
debian_changelog="${release_directory}/OBS/debian.changelog"
cat - <<EOF > "${debian_changelog}.new"
libavcodec-test (${version}-1) stable; urgency=medium

  * Update to version ${version}

 -- MediaArea CI <info@mediaarea.net>  ${date_rfc2822}

EOF
sed -i "1e cat ${debian_changelog}.new" "${debian_changelog}"
rm -f "${debian_changelog}.new"

date_rpm="$(LC_ALL=C date -u '+%a %b %e %Y' | sed 's/  / /g')"
rpm_changelog="${release_directory}/OBS/libavcodec-test.changes"
cat - <<EOF > "${rpm_changelog}.new"
* ${date_rpm} MediaArea CI <info@mediaarea.net> - ${version}
- Update to version ${version}

EOF
sed -i "1e cat ${rpm_changelog}.new" "${rpm_changelog}"
rm -f "${rpm_changelog}.new"

#-----------------------------------------------------------------------
# Prepare sources
tmp_directory="$(mktemp -d)"
pushd "${tmp_directory}"
    cp -a "${root_directory}" "${sources}"
    rm -fr "${sources}/.git*"

    XZ_OPT=-9e tar -cJ --owner=root --group=root -f "${release_directory}/libavcodec-test_${version}.tar.xz" "${sources}"
    7za a -t7z -mx=9 -bd "${release_directory}/libavcodec-test_${version}.7z" "${sources}"
popd
rm -fr "${tmp_directory}"
