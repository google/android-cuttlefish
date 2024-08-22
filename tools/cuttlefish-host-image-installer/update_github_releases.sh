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

STABLE_VERSION="v$version"

echo "step 2: move stable release to new commit id"
commit_id=$(git rev-list -n 1 --tags refs/tags/"$STABLE_VERSION")
stable_commit_id=$(git rev-list -n 1 --tags refs/tags/stable)
if [[ "$commit_id" == "$stable_commit_id" ]]; then
  echo "same commit, no need to change code base"
else
  git tag -f stable $commit_id
  git push -f origin stable
fi

# update changelog and descriptions
echo "step 3: update changelog and descriptions in release"
RUN_ID=$(gh run list -w HostImage -L 1 --json databaseId | jq -r '.[0].databaseId')
RUN_NOTE=$(gh run list -w HostImage -L 1 --json displayTitle | jq -r '.[0].displayTitle')
RUN_DATE=$(gh run list -w HostImage -L 1 --json createdAt | jq -r '.[0].createdAt')
echo "" >> changelog
echo "Stable host image changes:" >> changelog
echo "Cuttlefish version ${STABLE_VERSION}." >> changelog
echo "${RUN_NOTE}. Artifacts created at ${RUN_DATE}. Run ID ${RUN_ID}" >> changelog
gh release edit latest --notes-file ./changelog
gh release edit stable --notes-file ./changelog

# copy result with version name
echo "step 4: copy result with version name"
cp cuttlefish_packages.7z cuttlefish_packages"_${STABLE_VERSION}".7z
cp u-boot.bin u-boot"_${STABLE_VERSION}".bin
cp preseed-mini.iso.xz preseed-mini"_${STABLE_VERSION}".iso.xz
cp meta_gigamp_packages.7z meta_gigamp_packages"_${STABLE_VERSION}".7z
cp orchestration-image-x86_64.tar orchestration-image-x86_64"_${STABLE_VERSION}".tar
cp orchestration-image-arm64.tar orchestration-image-arm64"_${STABLE_VERSION}".tar

# upload assets to both latest and stable versions
echo "step 5: upload assets to both latest and stable versions"
changed_releases=("latest" "stable")
for version in "${changed_releases[@]}"
do
  ASSETS_LIST=$(gh release view "$version" --json assets --jq '.assets.[].name')
  stringarray=($ASSETS_LIST)
  for i in "${stringarray[@]}"
  do
    gh release delete-asset "$version" "$i" -y
  done

  gh release upload "$version" u-boot"_${STABLE_VERSION}".bin
  gh release upload "$version" preseed-mini"_${STABLE_VERSION}".iso.xz
  gh release upload "$version" cuttlefish_packages"_${STABLE_VERSION}".7z
  gh release upload "$version" meta_gigamp_packages"_${STABLE_VERSION}".7z
  gh release upload "$version" orchestration-image-x86_64"_${STABLE_VERSION}".tar
  gh release upload "$version" orchestration-image-arm64"_${STABLE_VERSION}".tar

  gh release upload "$version" u-boot.bin
  gh release upload "$version" preseed-mini.iso.xz
  gh release upload "$version" cuttlefish_packages.7z
  gh release upload "$version" meta_gigamp_packages.7z
  gh release upload "$version" orchestration-image-x86_64.tar
  gh release upload "$version" orchestration-image-arm64.tar
done
