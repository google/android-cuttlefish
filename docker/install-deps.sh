#!/bin/bash

set -o errexit
set -u
# set -x

source utils.sh

# $1 = parent
# $2 = parent version
# $3 = name
# $4 = version
# $5 = op
# $6 = filter
# $7 = process

function install_deps {
  local parent="$1"
  local parent_version="$2"
  local name="$3"
  local version="$4"
  local op="$5"
  local filter="$6"
  local process="$7"
  local path="$8"

  local installed_name_and_version=$(is_installed ${name} ${version} ${op})
  if [ -n "${installed_name_and_version}" ]; then
    echo "Package ${installed_name_and_version} is already installed (requested ${op} ${version})"
    return
  fi

  # assume the available debian package is at the appropriate version
  local deb=$(get_deb_from_name ${path} ${name})
  if [ -z "${deb}" ]; then
    echo Cannot locate deb file for package ${name} 1>&2
    return
  fi

  ./install-with-deps.sh "${name}" "${filter}" "${path}"

  echo "### INSTALL ${deb}"

  if [ -f "${path}/ignore-depends-for-${name}.txt" ]; then
    local ignores=$(cat ${path}/ignore-depends-for-${name}.txt)
    ignores=${ignores::-1}
    ignores="--ignore-depends=${ignores}"
    dpkg "${ignores}" --skip-same-version --install "${deb}"
  else
    echo "Cannot locate ${path}/ignore-depends-for-${name}.txt"
    dpkg --install "${deb}"
  fi
}

install_deps $*
