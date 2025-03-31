#!/usr/bin/env bash

# It builds the RPM packages and then spins up a container with these preinstalled.
# `scripts/install.sh` produces a bloated image, unless mounting `/root/.cache` directory
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
cd "${REPO_DIR}/docker/rpm-builder" || exit

# Build the RPM packages and the Docker image.
# https://docs.docker.com/reference/cli/docker/buildx/#examples
[ ! -d "${REPO_DIR}/tools/rpmbuild/RPMS/x86_64" ] && ./build_rpm_spec.sh
docker buildx create --use --name android-cuttlefish rhel-integration
docker buildx create --append --target integration
docker buildx create --append --tag android-cuttlefish/rhel-integration:1.2.0
docker buildx create --append --tag android-cuttlefish/rhel-integration:latest
docker buildx build --platform linux/amd64
docker build .
