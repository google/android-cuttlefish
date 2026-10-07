#!/usr/bin/env bash

# This shell script exists for building container images.
# Container image includes HO(Host Orchestrator) inside,
# so it could execute CF instance with API in HO.

script_location=`realpath -s $(dirname ${BASH_SOURCE[0]})`
android_cuttlefish_root_dir=$(realpath -s $script_location/../..)

usage() {
  echo "usage: $0 [-t <tag>] [-m <mode>] [-c <container_type>] [-M]"
  echo "  -t: name or name:tag of container image (default: cuttlefish-orchestration)"
  echo "  -m: set mode for build image (default: stable)"
  echo "      stable   - Downloads and installs host packages from stable channel"
  echo "      unstable - Downloads and installs host packages from unstable channel"
  echo "      nightly  - Downloads and installs host packages from nightly channel"
  echo "      dev      - Use *.deb files under repo dir as prebuilt of host packages"
  echo "  -c: type of container (default: docker)"
  echo "      Available container type: docker, podman"
  echo "  -M: include cuttlefish-metrics package in the container image"
}

name=cuttlefish-orchestration
mode=stable
container_type=docker
include_metrics=false
while getopts ":hm:t:c:M" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    c)
      container_type="${OPTARG}"
      ;;
    m)
      mode="${OPTARG}"
      ;;
    t)
      name="${OPTARG}"
      ;;
    M)
      include_metrics=true
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

case "${mode}" in
  stable)
    build_option=prod
    repo=android-cuttlefish
    ;;
  unstable)
    build_option=prod
    repo=android-cuttlefish-unstable
    ;;
  nightly)
    build_option=prod
    repo=android-cuttlefish-nightly
    ;;
  dev)
    build_option=dev
    repo=
    ;;
  *)
    echo "Invalid mode: ${mode}" >&2
    usage
    exit 1
esac

case "${container_type}" in
  docker)
    DOCKER_BUILDKIT=1
    ;;
  podman)
    ;;
  *)
    echo "Invalid container type: ${container_type}" >&2
    usage
    exit 1
esac

pushd $android_cuttlefish_root_dir
"${container_type}" build \
  --force-rm \
  --no-cache \
  -f container/image/Containerfile \
  -t "${name}" \
  --target runner \
  --build-arg "BUILD_OPTION=${build_option}" \
  --build-arg "REPO=${repo}" \
  --build-arg "INCLUDE_METRICS=${include_metrics}" \
  .
popd
