#!/bin/bash
#
# at /path/to/android-cuttlefish, run like this:
#  ./debs-builder-docker/build-debs-docker.sh
#
resource_subdir=debs-builder-docker
# 1: host package builder's docker image tag
# 2: host out dir where debian packages would be stored
function build_host_debian_pkg {
  local builder_tag="$1"
  docker build \
      --target "cuttlefish-hostpkg-builder" \
      -t "$builder_tag" \
      -f $resource_subdir/Dockerfile.debbld \
      $PWD \
       --build-arg UID="${UID}" \
       --build-arg OEM="${OEM}"

  local out_on_host="$2"
  shift 2

  # in guest:
  #  build:
  #    - android-cuttlefish (src)
  #    - out
  # host 2 guest maps:
  #  PWD -> build/androd-cuttlefish
  #  PWD/out -> build/out
  #
  local guest_home="/home/vsoc-01"
  local build_dir_on_guest="$guest_home/build"
  local src_on_guest="$build_dir_on_guest/android-cuttlefish"
  local script_on_guest="$src_on_guest/$resource_subdir/build-hostpkg.sh"
  local out_on_guest="$build_dir_on_guest/out"

  # run the script inside the guest
  # the script figures out its location in the guest file system
  # then, it does cd to the location, cd .., and run commands
  docker run \
         --rm --privileged \
         --user="vsoc-01" -w "$guest_home" \
         -v $PWD:$src_on_guest \
         -v $PWD/out:$out_on_guest \
         -t $hostpkg_builder \
         $script_on_guest $out_on_guest
}

hostpkg_builder="cuttlefish-deb-builder"
hostpkg_out_dir="$PWD/out"
build_host_debian_pkg $hostpkg_builder $hostpkg_out_dir
