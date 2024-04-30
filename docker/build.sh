#!/bin/bash

# This script is intended to:
#   build .deb packages for the host and/or for cuttlefish docker images
#   build the cuttlefish docker images, using the .deb packages & Dockerfile

function get_script_dir {
  echo "$(dirname ${BASH_SOURCE[0]})"
}

cd "$(get_script_dir)"

source "shflags"
source utils.sh

DEFINE_boolean verbose true "verbose mode"
DEFINE_boolean detect_gpu \
               "$(is_debian_series && echo true || echo false)" \
               "Attempt to detect the GPU vendor"
DEFINE_boolean rebuild_debs true \
               "Whether always rebuild .deb packages or reuse .deb files when available"
DEFINE_boolean rebuild_debs_verbose false \
               "When rebuilding deb packages, show the progress in stdin." "r"
DEFINE_boolean build_debs_only false \
               "To build host .deb packages only, not the cuttlefish docker images" "p"
DEFINE_boolean download_chrome false \
               "force to download chrome to the out directory" "d"


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

#1: OEM by reference
function calc_oem() {
  local -n ref_oem=$1
  ref_oem=
  if [[ ${FLAGS_detect_gpu} -eq ${FLAGS_TRUE} ]]; then
    if is_debian_series; then
      ref_oem=$(detect_gpu)
    else
      echo "Warning: --detect_gpu works only on Debian-based systems"
      echo
    fi
  fi

  if [ -z "${ref_oem}" ]; then
    echo "###"
    echo "### Building without physical-GPU support"
    echo "###"
  else
    echo "###"
    echo "### GPU is ${ref_oem}"
    echo "###"
  fi
}

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
    pushd gpu/"${oem}"/driver-deps 1>/dev/null
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

#1: "true" or "false", "true" means "--no-cache"
#2: x$OEM, note that $OEM could be empty
function build_docker_image {
  echo "###"
  echo "### Building docker image"
  echo "###"
  local no_cache=$1
  local oem=${2:1} # cut 'x' from $2
  shift 2

  local -a docker_targets=("cuttlefish-softgpu")

  if [ -n "${oem}" ]; then
      rm -f deps.txt equivs.txt ignore-depends-for-*.txt
      rm -f gpu/"${oem}"/driver.txt
      mkdir -p "gpu/${oem}/driver-deps"
      gpu/"${oem}"/driver.sh gpu/"${oem}"/filter-in-deps.sh | while read -e stem version; do
        if [ -n "$(is_installed "${stem}")" ]; then
          echo '###'
          echo "### ${stem}"
          echo '###'
          echo "${stem}" "${version}" >> gpu/"${oem}"/driver.txt
          process_one "${stem}" "${oem}"
        fi
      done
      mv -t gpu/"${oem}"/driver-deps/ deps.txt equivs.txt ignore-depends-for-*.txt
      docker_targets+=("cuttlefish-hwgpu")
  fi

  docker_build_opts=("-t" "cuttlefish")
  if [[ ${no_cache} == "true" ]]; then
    # when .debs were rebuilt, to be safe
    # the intermediate image layers should be rebuilt as well
    docker_build_opts+=("--no-cache")
  fi

  local uid=$UID
  if [[ -z $uid ]]; then
    uid=$(id -u)
  fi

  for target in "${docker_targets[@]}"; do
    docker build \
        --target "${target}" \
        ${docker_build_opts[@]} \
        "${PWD}" \
        --build-arg UID="${uid}" \
        --build-arg OEM="${oem}"
  done

  # don't nuke the cache
  # rm -fv gpu/${oem}/driver-deps
}

function is_rebuild_debs() {
  local -a required_packages=("cuttlefish-base" \
                              "cuttlefish-user" \
                              "cuttlefish-common" \
                              "cuttlefish-integration" \
                              "cuttlefish-orchestration")
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
  echo "### Building .deb Host Packages"
  echo "###"
  local verbosity="--noverbose"
  if [[ ${FLAGS_rebuild_debs_verbose} -eq ${FLAGS_TRUE} ]]; then
    verbosity="--verbose"
  fi
  pushd debs-builder-docker
  ./main.sh "${verbosity}"
  popd
}

function get_google_chrome_deb_name() {
  echo "google-chrome-stable_current_amd64.deb"
}

function is_download_chrome() {
  if ! [[ -f out/"$(get_google_chrome_deb_name)" ]]; then
    return 0
  fi
  if [[ ${FLAGS_download_chrome} -eq ${FLAGS_TRUE} ]]; then
    return 0
  fi
  return 1
}

function download_chrome() {
  echo "###"
  echo "### Downloading chrome"
  echo "###"
  local dest_dir=out
  local deb_file_name="$(get_google_chrome_deb_name)"
  # clean up $dest_dir
  rm -f $dest_dir/google-chrome*.deb || /bin/true
  if ! wget -P "$dest_dir"/ \
       https://dl.google.com/linux/direct/$deb_file_name; then
    >&2 echo "wget https://dl.google.com/linux/direct/$deb_file_name failed."
    exit 6
  fi
  return 0
}

function build_main() {
  local oem=""
  calc_oem oem

  # if .deb file(s) are rebuilt offline, the intermediate layers
  # should be rebuilt to be safe.
  #
  # Docker build is not aware of the time stamp or any change in the
  # .deb files we use to build the cuttlefish image.

  local no_cache="false" # whether we give --no-cache to docker build
  if is_rebuild_debs; then
    if do_rebuild_debs; then
      no_cache="true"
    else
      echo "Failed to build .deb host packages, rerun with --rebuild_debs_verbose for details"
      exit 1
    fi
  fi

  if [[ ${FLAGS_build_debs_only} -eq ${FLAGS_TRUE} ]]; then
    exit 0
  fi

  if is_download_chrome; then
    download_chrome
    no_cache="true"
  fi

  build_docker_image "$no_cache" "x$oem""$*"
}

if [[ ${FLAGS_verbose} -eq ${FLAGS_TRUE} ]]; then
  build_main
else
  build_main > /dev/null
fi
