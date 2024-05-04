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
# true, we invoke a second script in $3 (with the same arguments as the script in
# $2.

source utils.sh

function parse_deps {
  local package=$1
  local filter=$2
  local process=$3

  local package_and_arch=$(add_arch "${package}")
  local package_version=$(dpkg-query -W -f='${Version}' "${package_and_arch}")
  if [ -z "${package_version}" ]; then
    package_version=_
  fi

  parse_dpkg_dependencies "${package}" \
                          "${package_version}" \
                          "${filter}" \
                          "${process}" \
                          "$(get_depends_from_package ${package_and_arch})"
}

parse_deps $*
