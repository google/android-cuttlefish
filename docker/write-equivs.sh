#!/bin/bash

set -o errexit
# set -x

source utils.sh

# $1 = parent
# $2 = parent version
# $3 = name
# $4 = version
# $5 = op
# $6 = filter
# $7 = process

function write_equivs {
  local parent="$1"
  local parent_version="$2"
  local name="$3"
  local version="$4"
  local op="$5"
  local filter="$6"
  local process="$7"

  if [ -z "$(is_installed ${name} ${version} ${op})" ]; then
    return
  fi

  if ! grep -q -E "^${name}$|^${name} " equivs.txt; then
    local package_and_arch=$(add_arch "${name}")
    local installed_version=$(dpkg-query -W -f='${Version}' "${package_and_arch}")
    if [ "${version}" != '_' ]; then
      if ! dpkg --compare-versions ${installed_version} ${op} ${version}; then
        echo Installed package ${package_and_arch} version ${installed_version} not ${op} to/than ${version} 2>&1
        exit 1
      fi
    else
      # The dependency did not specify a version; we use the installed version
      # and the eq operator
      op=eq
    fi
    printf "%-${SHLVL}s" " "
    echo "${name}" "${installed_version}" "${op}" | tee -a equivs.txt
    ./parse-deps.sh "${name}" "${filter}" "${process}"
  fi
}

touch equivs.txt
write_equivs $*


