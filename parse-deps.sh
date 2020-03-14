#!/bin/bash

set -o errexit
set -u
# set -x

# $1 = package name
# $2 = filter script
# $3 = process script

# Given the name of a package in $1, discover the name and version of each of
# its direct dependencies.  For each direct dependency, invoke a script in $2
# with the name and version of that dependency.  If the script result tests
# true, we invoke a seond script in $3 (with the same arguments as the script in
# $2.

source utils.sh

function parse_deps {
  package=$1
  package_and_arch=$(add_arch "${package}")
  package_version=$(dpkg-query -W -f='${Version}' "${package_and_arch}")
  if [ -z "${package_version}" ]; then
    package_version=_
  fi

  local -a PACKAGES
  local OPTION_PKG
  local -a ELEMENTS

  IFS=',' read -ra PACKAGES <<< $(get_depends_from_package ${package_and_arch})

  for OPTION_PKG in "${PACKAGES[@]}"; do
    IFS='|' read -ra OPTIONS <<< "${OPTION_PKG}"
    for OPTION in "${OPTIONS[@]}"; do
      IFS=' ' read -ra ELEMENTS <<< "$OPTION"
      parse_name_version ${package} ${package_version} $2 $3 "${ELEMENTS[@]}"
    done
  done
}

parse_deps $*
