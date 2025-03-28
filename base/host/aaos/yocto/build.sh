#!/bin/sh

# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT
set -e

readonly WORK_DIR="/tmp/yocto-build-output"
readonly COMMAND="source assets/cuttlefish-sa8775.sh"

podman build --tag yocto-on-debian .
podman run --memory 0 --memory-swap -1 yocto-on-debian \
    bash -c "${COMMAND}"

echo "Done."
