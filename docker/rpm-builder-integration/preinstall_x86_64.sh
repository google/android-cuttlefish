#!/usr/bin/env bash

# It builds the RPM packages and then spins up a container with these preinstalled.
# `scripts/install.sh` produces a bloated image, unless mounting `/root/.cache` directory
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
cd "${REPO_DIR}/docker/rpm-builder-integration" || exit

# Build the RPM packages, when directory `x86_64` is not present.
# https://docs.docker.com/reference/cli/docker/buildx/#examples
[ ! -d "${REPO_DIR}/tools/rpmbuild/RPMS/x86_64" ] && "${REPO_DIR}/docker/rpm-builder/build_rpm_spec.sh"

# And then build the Docker image, which depends on these.
docker buildx create --use --file docker/rpm-builder-integration/arm_64/Dockerfile
docker buildx create --append --name android-cuttlefish rhel-integration
docker buildx create --append --tag android-cuttlefish/rhel-integration:latest
docker buildx create --append --tag android-cuttlefish/rhel-integration:1.3.0
docker buildx create --append --platform linux/amd64
docker buildx build
