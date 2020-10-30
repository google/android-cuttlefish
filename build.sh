#!/bin/bash

# tell if the distro is Debian
function is_debian_distro {
  if [[ -f /etc/debian_version ]]; then
      return 0
  fi
  # debian based distro mostly have /etc/debian_version
  # if ever not, use the whitelists
  if ls -1 /etc/*release | egrep "(debian|buntu|mint)" > /dev/null 2>&1; then
      return 0
  fi
  return 1
}

source "shflags"

DEFINE_boolean detect_gpu \
               "$(is_debian_distro && echo true || echo false)" \
               "Attempt to detect the GPU vendor"
DEFINE_boolean rebuild_debs true "Rebuild deb packages. If false, builds only when any .deb is missing in ./out/"
DEFINE_boolean rebuild_debs_verbose false "When rebuilding deb packages, show the progress in stdin."

FLAGS "$@" || exit 1

if [ "${FLAGS_help}" -eq ${FLAGS_TRUE} ]; then
  exit 0
fi

set -o errexit
set -u
# set -x

function detect_gpu {
  local completions=$(compgen -G gpu/*)
  for completion in ${completions}; do
    if "${completion}"/probe.sh; then
      echo "${completion//gpu\/}"
    fi
  done
}

OEM=
if [[ ${FLAGS_detect_gpu} -eq ${FLAGS_TRUE} ]]; then
  if is_debian_distro; then
      OEM=$(detect_gpu)
  else
      echo "Warning: --detect_gpu works only on Debian-based systems"
      echo
  fi
fi

if [ -z "${OEM}" ]; then
  echo "###"
  echo "### Building without physical-GPU support"
  echo "###"
else
  echo "###"
  echo "### GPU is ${OEM}"
  echo "###"
fi

source utils.sh

# $1 = package name
# $2 = OEM
# external context: reads/writes to deps.txt
function process_one {
  local stem=$1
  local oem=$2

  echo "Building dependency graph for ${stem}"
  ./walk-deps.sh _ _ "${stem}" _ _ ./gpu/"${oem}"/filter-in-deps.sh ./walk-deps.sh
  echo "Done"

  local package_and_arch=$(add_arch "${stem}")
  echo "${stem}" $(dpkg-query -W -f='${Version}' "${package_and_arch}") >> deps.txt
  sort -u deps.txt -o deps.txt

  set +o errexit # apt download may fail
  echo "Extracting debian packages for ${stem}"
  cat deps.txt | while read -e pkg version; do
    pushd gpu/"${OEM}"/driver-deps 1>/dev/null
    if [ -z "$(compgen -G "${pkg}"_"${version//:/%3a}"_\*.deb)" ]; then
      echo "Attempting to download debian package for ${pkg} version ${version}."
      if ! apt-get download "${pkg}"="${version}" 1>/dev/null; then
        echo "Attempting to reconstitute debian package for ${pkg} version ${version}."
        if [ ! -f $(which dpkg-repack) ]; then
          echo "Install dpkg-repack: sudo apt-install -y pkg-repack" 1>&2
          exit 1
        fi
        dpkg-repack "${pkg}"
        if [ ! -f "${pkg}"_"${version}"_*.deb ]; then
          echo "Debian package ${pkg} version ${version} could be neither downloaded nor repacked" 1>&2
          exit 1
        fi
      fi
      echo "Succeeded"
    else
      echo "Found file for package ${pkg} version ${version}. "
    fi
    popd 1>/dev/null
  done
  echo "Done"
  set -o errexit

  ./parse-deps.sh "${stem}" "./gpu/${oem}/filter-out-deps.sh" "./write-equivs.sh"
}

function build_docker_image {

  local -a docker_targets=("cuttlefish-softgpu")

  if [ -n "${OEM}" ]; then
      rm -f deps.txt equivs.txt ignore-depends-for-*.txt
      rm -f gpu/"${OEM}"/driver.txt
      mkdir -p "gpu/${OEM}/driver-deps"
      gpu/"${OEM}"/driver.sh gpu/"${OEM}"/filter-in-deps.sh | while read -e stem version; do
        if [ -n "$(is_installed "${stem}")" ]; then
          echo '###'
          echo "### ${stem}"
          echo '###'
          echo "${stem}" "${version}" >> gpu/"${OEM}"/driver.txt
          process_one "${stem}" "${OEM}"
        fi
      done
      mv -t gpu/"${OEM}"/driver-deps/ deps.txt equivs.txt ignore-depends-for-*.txt
      docker_targets+=("cuttlefish-hwgpu")
  fi

  for target in "${docker_targets[@]}"; do
    docker build \
        --target "${target}" \
         -t 'cuttlefish' \
        "${PWD}" \
        --build-arg UID="${UID}" \
        --build-arg OEM="${OEM}"
  done

  # don't nuke the cache
  # rm -fv gpu/${OEM}/driver-deps
}

function is_rebuild_debs() {
  local -a required_packages=("cuttlefish-common" \
                              "cuttlefish-integration" \
                              "cuttlefish-integration-dbgsym")
  if [[ ${FLAGS_rebuild_debs} -eq ${FLAGS_TRUE} ]]; then
      return 0
  fi
  for comp in "${required_packages[@]}"; do
      if ! ls -1 ./out | egrep "$comp" | egrep "*\.deb$" > /dev/null 2>&1; then
          return 0
      fi
  done
  return 1
}

function do_rebuild_debs() {
  echo "###"
  echo "### Building ,deb Host packages"
  echo "###"
  local verbosity="--noverbose"
  if [[ ${FLAGS_rebuild_debs_verbose} -eq ${FLAGS_TRUE} ]]; then
    verbosity="--verbose"
  fi
  ./debs-builder-docker/build-debs-with-docker.sh "${verbosity}"
}

if is_rebuild_debs; then
  do_rebuild_debs
fi
build_docker_image "$*"
