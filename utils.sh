#!/bin/bash

# return: debian name for this machine's architecture
function get_arch {
  echo -n $(dpkg --print-architecture)
}

# $1 = package name (e.g., libc6
# return: package name followed by the architecture (e.g., libc6:amd64)
function add_arch {
  set +o errexit
  local package_arch=$(dpkg-query -W -f='${Architecture}' $1:$(get_arch) $1:all 2>/dev/null)
  set -o errexit
  if [ -z "${package_arch}" -o "${package_arch}" == "all" ]; then
    local package="$1"
  else
    local package="$1:${package_arch}"
  fi
  echo -n $package
}

# $1 = input string
# $2 = input stream with leading and trailing spaces removed
function trim {
  local leading="${*#"${*%%[![:space:]]*}"}"
  local trailing="${leading%"${leading##*[![:space:]]}"}"
  printf '%s' "$trailing"
}

# $1 = directory path to .deb files
# $2 = package name
# $3 = package version
# return: path/to/.deb file
function get_deb_from_name {
  local path=$1
  local name=$2
  set +u
  local version=$3
  set -u
  if [[ -n "${version}" && "${version}" != '_' ]]; then
    local deb=$(find ${path} -name ${name}_${version//:/%3a}_\*.deb -printf '%p')
  else
    local deb=$(find ${path} -name ${name}_\*.deb -printf '%p')
  fi
  echo -n "${deb}"
}

# $1 = path/to/.deb file
# return: version string
function get_version_from_deb {
  local deb=$1
  local version=$(dpkg-deb -W --showformat='${Version}' ${deb})
  echo -n ${version}
}

# $1 = name:arch of debian package
# return: comma-separated packages from the Pre-Depends and Depends version strings
function get_depends_from_package {
  local name_and_arch=$1
  local result=$(dpkg-query -W -f='${Pre-Depends}' ${name_and_arch})
  if [ -n "${result}" ]; then
    result="${result}, $(dpkg-query -W -f='${Depends}' ${name_and_arch})"
  else
    result="$(dpkg-query -W -f='${Depends}' ${name_and_arch})"
  fi
  echo -n $(trim ${result})
}

# $1 = path/to/.deb file
# return: comma-separated packages from the Pre-Depends and Depends version strings
function get_depends_from_deb {
  local deb=$1
  local result=$(dpkg-deb -W --showformat='${Pre-Depends}' ${deb})
  if [ -n "${result}" ]; then
    result="${result}, $(dpkg-deb -W --showformat='${Depends}' ${deb})"
  else
    result="$(dpkg-deb -W --showformat='${Depends}' ${deb})"
  fi
  echo -n $(trim ${result})
}

# $1 = package name (no version)
# $2 = version (optional)
# $3 = op (optional), assumed 'eq'; if provided, version must also be specified
# return: if a package is installed, and its installed version ${op} specified
#         version checks out, then return the installed version of the package
#         if not installed, the empty string.
function is_installed {
  local name=$1
  set +u
  local version=$2
  local op=$3
  set -u
# if [ -n "$(dpkg-query -W -f='${binary:Package}\n' | grep -E """^${name}$|^${name}:""")" ]; then
  local package_and_arch=$(add_arch "${name}")
  if [ 'installed' == "$(dpkg-query -W -f='${db:Status-Status}' ${package_and_arch} 2>/dev/null)" ]; then
   if [ "${version}" == '_' ]; then
      version=
    fi
    local installed_version="$(dpkg-query -W -f='${Version}' ${package_and_arch})"
    if [[ -z "${version}" && -z "${installed_version}" ]]; then
      echo "${package_and_arch}"
      return
    fi
    if [[ -n "${version}" && -n "${installed_version}" ]]; then
      if [ -z "${op}" ]; then
        op='eq'
      fi
      if ! dpkg --compare-versions ${installed_version} ${op} ${version}; then
        echo ""
      else
        echo "${package_and_arch} ${installed_version}"
      fi
    else
      echo "${package_and_arch} ${installed_version}"
    fi
    return
  fi
# fi
  echo ""
}

# $1 = parent name
# $2 = parent version
# $3 = filter script
# $4 = processing script
# $5 = name of dependency
# $@ = rest of dependency string from dpkg-query
# END = if present, everything following that is passed to $3 and $4
function parse_name_version {
  local parent=$1
  local parent_version=$2
  local filter=$3
  local process=$4
  local name=$5
  shift 5
  local version=_
  local op=_
  # echo "${name}"
  for el in "$@"; do
    shift
    case $el in
    '(=' )
      op=eq
      ;;
    '(<' | '(<<' )
      op=lt
      ;;
    '(<=' )
      op=le
      ;;
    '(>' | '(>>' )
      op=gt
      ;;
    '(>=' )
      op=ge
      ;;
    END )
      break
      ;;
    * )
      version=${el::-1}
      ;;
    esac
  done
  if ${filter} ${parent} ${parent_version} ${name} ${version} ${op} ${filter} ${process} $*; then
    ${process} ${parent} ${parent_version} ${name} ${version} ${op} ${filter} ${process} $*
  fi
}

# $1 = package name
# $2 = package version
# $3 = filter script
# $4 = process script
# $@ = dependency string from dpkg-query
# END = if present, everything following that is passed as additional context to
#       the callback functions and scripts
function parse_dpkg_dependencies {
  local name=$1
  local package_version=$2
  local filter=$3
  local process=$4
  shift 4

  local -a args=( "$@" )
set +o errexit # i hate bash
  let i=0
set -o errexit
  for el in "${args[@]}"; do
     [[ "${el}" == 'END' ]] && break
set +o errexit
     let i++
set -o errexit
  done

  local -a PACKAGES
  local OPTION_PKG
  local -a ELEMENTS

  IFS=',' read -ra PACKAGES <<< "${args[@]:0:${i}}"
  for OPTION_PKG in "${PACKAGES[@]}"; do
    IFS='|' read -ra OPTIONS <<< "${OPTION_PKG}"
    for OPTION in "${OPTIONS[@]}"; do
      IFS=' ' read -ra ELEMENTS <<< "$OPTION"
      parse_name_version "${name}" "${package_version}" "${filter}" "${process}" "${ELEMENTS[@]}" "${args[@]:${i}}"
    done
  done
}

# return if this is exactly Debian Linux
function is_debian {
  if [[ -f /etc/debian_version ]]; then
      return 0
  fi
  if ls -1 /etc/*release | egrep -i "debian" > /dev/null 2>&1; then
    return 0
  fi
  return 1
}

# tell if the distro is Debian series
function is_debian_series {
  if is_debian; then
    return 0
  fi
  # if ever not Debian, use the list of distros known to be a variant of Debian
  # So, extend this list if more distros should be supported
  if ls -1 /etc/*release | egrep -i "(buntu|mint)" > /dev/null 2>&1; then
      return 0
  fi
  return 1
}
