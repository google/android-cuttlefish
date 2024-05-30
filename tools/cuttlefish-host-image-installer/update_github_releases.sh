#!/bin/bash

set -e

# should update tags before do anything
echo "step 1: parse changelog to get the versions"
LATEST_VERSION="v"$(python version_parser.py ../../base/debian/changelog latest)
rm -f ./changelog
STABLE_VERSION="v"$(python version_parser.py ../../base/debian/changelog stable)
RELEASE_TAG_LIST=$(gh release list --json tagName --jq '.[].tagName')
stringarray=($RELEASE_TAG_LIST)
index=0
stable_index=-1
for i in "${stringarray[@]}"
do
  if [[ "$i" == "stable" ]]
  then
    stable_index=$index
  fi
  let index=index+1
done

echo "step 2: update the stable tag"
RELEASE_NAME_LIST=$(gh release list --json name --jq '.[].name')
stringarray=($RELEASE_NAME_LIST)
index=0
for i in "${stringarray[@]}"
do
  if [[ "$index" == "$stable_index" ]]
  then
    if [[ "$i" != "$STABLE_VERSION" ]]
    then
      gh release edit stable --tag "$i"
      gh release edit "$STABLE_VERSION" --tag stable
    fi
  fi
  let index=index+1
done
