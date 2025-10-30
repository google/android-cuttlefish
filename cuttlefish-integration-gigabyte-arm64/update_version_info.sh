#!/bin/bash

set -e

# should update tags before do anything
echo "step 1: parse changelog to get the versions"
#rm -f ./changelog
CHANGELOG=$(mktemp)
version=$(dpkg-parsechangelog --show-field version -c 1 -o 0 -l debian/changelog)
distribution=$(dpkg-parsechangelog --show-field distribution -c 1 -o 0 -l debian/changelog)
changes=$(dpkg-parsechangelog -c 1 -o 0 -l debian/changelog)
echo "" >> "${CHANGELOG}"
echo "$changes" >> "${CHANGELOG}"

# update changelog and descriptions
echo "step 2: update changelog and descriptions in release"
CREATE_DATE=$(date)
echo "Image was created at ${CREATE_DATE}" >> "${CHANGELOG}"

mv "${CHANGELOG}" usr/share/version_info

