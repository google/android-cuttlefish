#!/usr/bin/env bash

# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Build custom image to be used in GCE VMs.

set -o errexit -o nounset -o pipefail

function print_usage() {
  >&2 echo "usage: $0 -p /path/to/packages_dir [-r /path/to/disk.raw]"
}

PACKAGES_DIR=
DISK_RAW=

while getopts ":hp:r:" opt; do
  case "${opt}" in
    h)
      usage
      exit
      ;;
    p)
      PACKAGES_DIR="${OPTARG}"
      ;;
    r)
      DISK_RAW="${OPTARG}"
      ;;
    *)
      echo "Invalid option: -${OPTARG}"
      usage
      exit 1
      ;;
  esac
done

readonly PACKAGES_DIR

if [[ "${PACKAGES_DIR}" == "" ]] || ! [[ -d "${PACKAGES_DIR}" ]]; then
  >&2 echo "invalid package directory"
  print_usage
  exit 1
fi

if [[ "${DISK_RAW}" == "" ]] then
  readonly IMAGE_NAME="debian-12-backports-genericcloud-amd64-20250428-2096.raw"
  pushd /tmp
  wget "https://cloud.debian.org/images/cloud/bookworm-backports/20250428-2096/${IMAGE_NAME}"
  popd
  DISK_RAW="/tmp/${IMAGE_NAME}"
fi

readonly DISK_RAW

readonly SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
readonly OUT_DIR=$(pwd)

rm -f ${OUT_DIR}/disk.raw
cp ${DISK_RAW} ${OUT_DIR}/disk.raw

bash ${SCRIPT_DIR}/increase_disk_size.sh -r ${OUT_DIR}/disk.raw
bash ${SCRIPT_DIR}/install_gce_guest_environment.sh -r ${OUT_DIR}/disk.raw
bash ${SCRIPT_DIR}/install_cf_packages.sh -r ${OUT_DIR}/disk.raw -p ${PACKAGES_DIR}

>&2 echo "compressing disk raw"
tar -czvf image.tar.gz -C ${OUT_DIR} disk.raw

