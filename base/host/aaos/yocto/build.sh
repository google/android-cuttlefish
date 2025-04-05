#!/bin/sh

# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT
set -e

readonly WORK_DIR="/tmp/yocto-build-output"
readonly COMMAND="./assets/meta-qcom-qcs9100.sh"

podman run --memory 0 --memory-swap -1 yocto-on-debian \
    bash -c "${COMMAND}"

echo "Done."
