#!/bin/bash

source "shflags"

DEFINE_boolean detect_gpu true "Attempt to detect the GPU vendor"

FLAGS "$@" || exit 1

if [ ${FLAGS_help} -eq ${FLAGS_TRUE} ]; then
  exit 0
fi

set -o errexit
set -u
# set -x

function detect_gpu {
  local completions=$(compgen -G gpu/*)
  for completion in ${completions}; do
    if ${completion}/probe.sh; then
      echo "${completion//gpu\/}"
    fi
  done
}


OEM=
if [[ ${FLAGS_detect_gpu} -eq ${FLAGS_TRUE} ]]; then
  OEM=$(detect_gpu)
else
  echo "###"
  echo "### Building without physical-GPU support"
  echo "###"
fi

if [ -n "${OEM}" ]; then
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
  ./walk-deps.sh _ _ ${stem} _ _ ./gpu/${oem}/filter-in-deps.sh ./walk-deps.sh
  echo "Done"

  local package_and_arch=$(add_arch ${stem})
  echo ${stem} $(dpkg-query -W -f='${Version}' ${package_and_arch}) >> deps.txt
  sort -u deps.txt -o deps.txt

  set +o errexit # apt download may fail
  echo "Extracting debian packages for ${stem}"
  cat deps.txt | while read -e pkg version; do
    pushd gpu/${OEM}/driver-deps 1>/dev/null
    if [ -z "$(compgen -G ${pkg}_${version//:/%3a}_\*.deb)" ]; then
      echo "Attempting to download debian package for ${pkg} version ${version}."
      if ! apt-get download ${pkg}=${version} 1>/dev/null; then
        echo "Attempting to reconstitute debian package for ${pkg} version ${version}."
        if [ ! -f $(which dpkg-repack) ]; then
          echo "Install dpkg-repack: sudo apt-install -y pkg-repack" 1>&2
          exit 1
        fi
        dpkg-repack ${pkg}
        if [ ! -f ${pkg}_${version}_*.deb ]; then
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
      rm -f gpu/${OEM}/driver.txt
      mkdir -p "gpu/${OEM}/driver-deps"
      gpu/${OEM}/driver.sh gpu/${OEM}/filter-in-deps.sh | while read -e stem version; do
        if [ -n "$(is_installed ${stem})" ]; then
          echo '###'
          echo "### ${stem}"
          echo '###'
          echo ${stem} ${version} >> gpu/${OEM}/driver.txt
          process_one ${stem} ${OEM}
        fi
      done
      mv -t gpu/${OEM}/driver-deps/ deps.txt equivs.txt ignore-depends-for-*.txt
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

build_docker_image $*
