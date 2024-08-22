#!/bin/bash

# This shell script exists for building docker image.
# Docker image includes HO(Host Orchestrator) inside,
# so it could execute CF instance with API in HO.

script_location=`realpath -s $(dirname ${BASH_SOURCE[0]})`
android_cuttlefish_root_dir=$(realpath -s $script_location/..)

if [[ "$1" == "" ]]; then
    tag=cuttlefish-orchestration
else
    tag=$1
fi

# Build docker image
pushd $android_cuttlefish_root_dir
docker build \
    --force-rm \
    --no-cache \
    -f docker/Dockerfile \
    -t $tag \
    .
popd
