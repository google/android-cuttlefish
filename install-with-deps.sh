#!/bin/bash
#
# Walk the dependency tree starting from a given package and install
# dependencies, previouisly provided as .deb files, in reverse-dependency order.
#
# $1 = package name
# $2 = filter script
# $3 = path to .deb files

set -o errexit
set -u
# set -x

source utils.sh

function walk_one_level {
  local name=${1/:any/}
  local filter=$2
  local path=$3
  local deb=$(get_deb_from_name ${path} ${name})
  if [ -z "${deb}" ]; then
    echo Cannot locate deb file for package ${name} 1>&2
    exit 1
  fi
  local package_version=$(get_version_from_deb ${deb})
  if [ -z "${package_version}" ]; then
    package_version=_
  fi

  parse_dpkg_dependencies "${name}" \
                          "${package_version}" \
                          "${filter}" \
                          ./install-deps.sh \
                          "$(get_depends_from_deb ${deb})" \
                          "END" "${path}"
}

walk_one_level $*
