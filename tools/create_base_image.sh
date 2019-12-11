#!/bin/bash

# Creates a base image suitable for booting cuttlefish on GCE

source "${ANDROID_BUILD_TOP}/device/google/cuttlefish_common/tools/create_base_image_hostlib.sh"

FLAGS "$@" || exit 1
main "${FLAGS_ARGV[@]}"
