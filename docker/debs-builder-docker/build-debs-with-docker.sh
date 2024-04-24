#!/bin/bash
#
# at /path/to/android-cuttlefish/docker, run like this:
#  ./debs-builder-docker/build-debs-docker.sh -o /path/to/dir
#

source "shflags"

DEFINE_boolean verbose "true" "show the stdout/stderr from the sub-tools" "v"
DEFINE_string out_dir "${PWD}/out" "indicates where to save the built debs" "o"

FLAGS "$@" || exit 1

if [ ${FLAGS_help} -eq ${FLAGS_TRUE} ]; then
    exit 0
fi

# 1: host package builder's docker image tag
# 2: out dir where debian packages would be stored
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

  local out_dir="$2"
  shift 2

  # Create the following directories in container
  #
  # host/
  # ├── android-cuttlefish (a copy of `android-cuttlefish` repository)
  # └── out                (mounted ${out_dir} to save the built deb files)
  #
  local guest_home="/home/vsoc-01"
  local guest_host_dir="$guest_home/host"
  local guest_repo_dir="$guest_host_dir/android-cuttlefish"
  local guest_out_dir="$guest_host_dir/out"
  local android_cuttlefish_on_host="$(realpath ..)"

  local container_name="meow_yumi"
  # in case of previous failure, we ensure we restart a new container
  docker rm -f $container_name
  if ! docker run -d \
       --rm --privileged \
       --user="vsoc-01" -w "$guest_home" \
       --name=$container_name \
       -i \
       $builder_tag; then
      >&2 echo "fail to start the docker container, $container_name, to build deb packages"
      exit 1
  fi

  if ! docker cp $android_cuttlefish_on_host/. $container_name:$guest_repo_dir > /dev/null 2>&1; then
      >&2 echo "fail to copy android-cuttlefish/* to the container, $container_name"
      exit 2
  fi

  # run the script inside the guest
  # the script figures out its location in the guest file system
  # then, it does cd to the location, cd .., and run commands
  local guest_script_path="$guest_repo_dir/docker/$resource_subdir/build-hostpkg.sh"
  if ! docker exec \
       --privileged \
       --user="vsoc-01" -w "$guest_home" \
       $container_name $guest_script_path $guest_out_dir; then
      >&2 echo "fail to exec/attach to the container, $container_name, running in background"
      exit 3
  fi

  if ! docker cp $container_name:$guest_out_dir/. ${out_dir} > /dev/null 2>&1; then
      >&2 echo "fail to copy deb files from container, $container_name"
      exit 2
  fi

  if ! docker rm -f $container_name > /dev/null 2>&1; then
      >&2 echo "fail to clean up to the container"
      exit 4
  fi
}

mkdir -p ${FLAGS_out_dir}

hostpkg_builder="cuttlefish-deb-builder"
if [ ${FLAGS_verbose} -eq ${FLAGS_TRUE} ]; then
    build_host_debian_pkg $hostpkg_builder ${FLAGS_out_dir}
else
    build_host_debian_pkg $hostpkg_builder ${FLAGS_out_dir} > /dev/null 2>&1
fi

