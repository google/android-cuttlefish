#!/usr/bin/env bash

# This shell script exists for building docker image.
# Docker image includes HO(Host Orchestrator) inside,
# so it could execute CF instance with API in HO.

script_location=`realpath -s $(dirname ${BASH_SOURCE[0]})`
android_cuttlefish_root_dir=$(realpath -s $script_location/..)

usage() {
  echo "usage: $0 [-t <tag>] [-m <mode>]"
  echo "  -t: name or name:tag of docker image (default cuttlefish-orchestration)"
  echo "  -m: set mode for build image (default: prod)"
  echo "      prod - Downloads and installs host packages"
  echo "      dev  - Use *.deb files under repo dir as prebuilt of host packages"

}

name=cuttlefish-orchestration
build_option=prod
while getopts ":hm:t:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    m)
      build_option="${OPTARG}"
      ;;
    t)
      name="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      usage
      exit 1
      ;;
  esac
done

# Build docker image
pushd $android_cuttlefish_root_dir
DOCKER_BUILDKIT=1 docker build \
    --force-rm \
    --no-cache \
    -f docker/Dockerfile \
    -t $name \
    --target runner \
    --build-arg BUILD_OPTION=$build_option \
    .
popd
