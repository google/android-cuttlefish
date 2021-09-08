#!/bin/bash

# Creates a base image suitable for booting cuttlefish on GCE

source "${ANDROID_BUILD_TOP}/device/google/cuttlefish/tools/create_base_image_hostlib.sh"

FLAGS "$@" || exit 1
if [[ "${FLAGS_help}" -eq 0 ]]; then
  echo ${FLAGS_help}
  exit 1
fi

set -o errexit
set -x
main "${FLAGS_ARGV[@]}"
