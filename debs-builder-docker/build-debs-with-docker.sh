#!/bin/bash
#
# at /path/to/android-cuttlefish, run like this:
#  ./debs-builder-docker/build-debs-docker.sh
#

source "shflags"

DEFINE_boolean verbose "true" "show the stdout/stderr from the sub-tools" "v"

FLAGS "$@" || exit 1

if [ ${FLAGS_help} -eq ${FLAGS_TRUE} ]; then
    exit 0
fi

# 1: host package builder's docker image tag
# 2: host out dir where debian packages would be stored
function build_host_debian_pkg {
  local builder_tag="$1"
  local resource_subdir=debs-builder-docker
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
  #  host:
  #   - android-cuttlefish (copy of the host android-cuttlefish)
  #   - out (mounted host out dir)
  # host 2 guest maps:
  #  PWD -> host/androd-cuttlefish
  #  PWD/out -> host/out
  #
  local guest_home="/home/vsoc-01"
  local host_dir_on_guest="$guest_home/host"
  local src_on_guest="$host_dir_on_guest/android-cuttlefish"
  local out_on_guest="$host_dir_on_guest/out"

  local container_name="meow_yumi"
  # in case of previous failure, we ensure we restart a new container
  docker rm -f $container_name
  if ! docker run -d \
       --rm --privileged \
       --user="vsoc-01" -w "$guest_home" \
       --name=$container_name \
       -v $PWD/out:$out_on_guest \
       -i \
       $builder_tag; then
      >&2 echo "fail to start the docker container, $container_name, to build deb packages"
      exit 1
  fi

  if ! docker cp . $container_name:$src_on_guest > /dev/null 2>&1; then
      >&2 echo "fail to copy android-cuttlefish/* to the container, $container_name"
      exit 2
  fi

  # run the script inside the guest
  # the script figures out its location in the guest file system
  # then, it does cd to the location, cd .., and run commands
  local script_on_guest="$src_on_guest/$resource_subdir/build-hostpkg.sh"
  if ! docker exec \
       --privileged \
       --user="vsoc-01" -w "$guest_home" \
       $container_name $script_on_guest $out_on_guest; then
      >&2 echo "fail to exec/attach to the container, $container_name, running in background"
      exit 3
  fi

  if ! docker rm -f $container_name > /dev/null 2>&1; then
      >&2 echo "fail to clean up to the container"
      exit 4
  fi
}

hostpkg_builder="cuttlefish-deb-builder"
hostpkg_out_dir="$PWD/out"
if [ ${FLAGS_verbose} -eq ${FLAGS_TRUE} ]; then
    build_host_debian_pkg $hostpkg_builder $hostpkg_out_dir
else
    build_host_debian_pkg $hostpkg_builder $hostpkg_out_dir > /dev/null 2>&1
fi

