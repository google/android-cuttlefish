#!/bin/bash

set -e

# should update tags before do anything
echo "step 1: parse changelog to get the versions"
rm -f ./changelog
index=0
distribution="latest"
version=""
changes=""
while [ "$distribution" != "" ] && [ "$distribution" != "stable" ]
do
  version=$(dpkg-parsechangelog --show-field version -c 1 -o "$index" -l ../../base/debian/changelog)
  distribution=$(dpkg-parsechangelog --show-field distribution -c 1 -o "$index" -l ../../base/debian/changelog)
  changes=$(dpkg-parsechangelog -c 1 -o "$index" -l ../../base/debian/changelog)
  let index=index+1
done
echo "$changes" > changelog

index=0
distribution="latest"
version=""
changes=""
while [ "$distribution" != "" ] && [ "$distribution" != "stable" ]
do
  version=$(dpkg-parsechangelog --show-field version -c 1 -o "$index" -l ../../frontend/debian/changelog)
  distribution=$(dpkg-parsechangelog --show-field distribution -c 1 -o "$index" -l ../../frontend/debian/changelog)
  changes=$(dpkg-parsechangelog -c 1 -o "$index" -l ../../frontend/debian/changelog)
  let index=index+1
done
echo "" >> changelog
echo "$changes" >> changelog

version=$(dpkg-parsechangelog --show-field version -c 1 -o 0 -l metapackage-google/debian/changelog)
distribution=$(dpkg-parsechangelog --show-field distribution -c 1 -o 0 -l metapackage-google/debian/changelog)
changes=$(dpkg-parsechangelog -c 1 -o 0 -l metapackage-google/debian/changelog)
echo "" >> changelog
echo "$changes" >> changelog

# update changelog and descriptions
echo "step 2: update changelog and descriptions in release"
RUN_ID=$(gh run list -w HostImage -L 1 --json databaseId | jq -r '.[0].databaseId')
RUN_NOTE=$(gh run list -w HostImage -L 1 --json displayTitle | jq -r '.[0].displayTitle')
RUN_DATE=$(gh run list -w HostImage -L 1 --json createdAt | jq -r '.[0].createdAt')
echo "" >> changelog
echo "Stable host image changes:" >> changelog
echo "Cuttlefish version ${STABLE_VERSION}." >> changelog
echo "${RUN_NOTE}. Artifacts created at ${RUN_DATE}. Run ID ${RUN_ID}" >> changelog
CREATE_DATE=$(date)
echo "Image was created at ${CREATE_DATE}" >> changelog

mv changelog metapackage-google/usr/share/version_info

