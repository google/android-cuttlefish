#!/bin/bash

# This shell script exists for building ARM64 docker image.
# Docker image includes HO(Host Orchestrator) inside,
# so it could execute CF instance with API in HO.

# Build debian packages(e.g. cuttlefish-base) with docker.
pushd ../debs-builder-docker
./main.sh
popd
mkdir out
mv ../debs-builder-docker/out/cuttlefish-*.deb ./out
rm -rf ../debs-builder-docker/out

# Build docker image
docker build --no-cache -t cuttlefish-arm64 $PWD

# Cleanup out directory
rm -rf out
