#!/usr/bin/env bash

# It builds the RPM packages and then spins up a container with these preinstalled.
# `scripts/install.sh` produces a bloated image, unless mounting `/root/.cache` directory
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
cd "${REPO_DIR}/docker/rpm-builder-orchestration" || exit
PACKAGES="${REPO_DIR}/tools/rpmbuild/RPMS/x86_64"
PLATFORM="linux/amd64"

if [ "$(uname -i)" = "aarch64" ]; then
  PACKAGES="${REPO_DIR}/tools/rpmbuild/RPMS/aarch64"
  PLATFORM="linux/arm64"
fi

# Build the RPM packages, when directory `x86_64` or `aarch64` is not present.
# https://docs.docker.com/reference/cli/docker/buildx/#examples
[ ! -d "$PACKAGES" ] && "${REPO_DIR}/docker/rpm-builder/build_rpm_spec.sh"

# And then build the Docker image, which depends on these.
docker buildx create --use
docker buildx create --append --file docker/rpm-builder-orchestration/Dockerfile
docker buildx create --append --name android-cuttlefish rhel-orchestration
docker buildx create --append --tag android-cuttlefish/rhel-orchestration:latest
docker buildx create --append --tag android-cuttlefish/rhel-orchestration:1.3.0
docker buildx create --append --platform $PLATFORM
docker buildx build
