#!/bin/bash

set -o errexit

# $1: package name
# $2: package version (defaults to 9999)
# $3: relationship (<<, <=, =, >=, >>)
function generate_empty_package {
  local _pkg=${1}
  local _control=${_pkg}.control
  local _version=${2}
  equivs-control ${_control}
  sed -i "s/<package name; defaults to equivs-dummy>/${_pkg}/g" ${_control}
  # Set a high package version to satisfy all dependencies
  sed -i "s/$(grep '^# Version' ${_pkg}.control)/Version: ${_version}/g" ${_control}
  equivs-build ${_control}
  dpkg -i ${_pkg}_${_version}_all.deb
}

case $3 in
  'lt' | 'gt' )
      echo Cannot handle less-than and greater-than 2>&1
      exit 1
  ;;
esac

dir=$(mktemp -d)
pushd $dir

generate_empty_package $1 $2 $3

popd
rm -rfv $dir
