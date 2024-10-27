#!/usr/bin/env bash

# This shell script exists for building Docker image using Docker Buildx.
# The image includes HO (Host Orchestrator) inside, so it can execute CF instance with API in HO.

set -e

script_location=$(realpath -s $(dirname "${BASH_SOURCE[0]}"))
android_cuttlefish_root_dir=$(realpath -s "$script_location/..")

if [[ "$1" == "" ]]; then
    tag="cuttlefish-orchestration"
else
    tag="$1"
fi

# Set up Docker Buildx if not already set up
docker buildx create --use || true

# Build the Docker image using Buildx for x86_64 and arm64 platforms
pushd "$android_cuttlefish_root_dir"
docker buildx build \
    --force-rm \
    --no-cache \
    -f docker/Dockerfile \
    -t "$tag" \
    --load .
popd
