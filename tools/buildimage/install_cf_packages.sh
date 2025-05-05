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

# Install relevant cuttlefish packages from https://github.com/google/android-cuttlefish.

# IMPORTANT!!! This script expects an `image_bullseye_gce_amd64.raw` file
# from https://salsa.debian.org/cloud-team/debian-cloud-images

set -o errexit -o nounset -o pipefail

function print_usage() {
  >&2 echo "usage: $0 -r /path/to/disk.raw -p /path/to/packages_dir"
}

DISK_RAW=
PACKAGES_DIR=

while getopts ":hr:p:" opt; do
  case "${opt}" in
    h)
      usage
      ;;
    r)
      DISK_RAW="${OPTARG}"
      ;;
    p)
      PACKAGES_DIR="${OPTARG}"
      ;;
    *)
      >&2 echo "Invalid option: -${OPTARG}"
      print_usage
      exit 1
      ;;
  esac
done

readonly DISK_RAW
readonly PACKAGES_DIR

if [[ "${DISK_RAW}" == "" ]]; then
  echo "path to disk raw is required"
  print_usage
  exit 1
fi

if [[ "${PACKAGES_DIR}" == "" ]] || ! [[ -d "${PACKAGES_DIR}" ]]; then
  echo "invalid packages directory"
  print_usage
  exit 1
fi

readonly MOUNT_POINT="/mnt/image"

sudo mkdir ${MOUNT_POINT}
# offset value is 262144 * 512, the `262144`th is the sector where the `Linux filesystem` partition
# starts and `512` bytes is the sectors size. See `sudo fdisk -l disk.raw`.
sudo mount -o loop,offset=$((262144 * 512)) ${DISK_RAW} ${MOUNT_POINT}

cp ${PACKAGES_DIR}/cuttlefish-base_*_amd64.deb ${MOUNT_POINT}/tmp/
cp ${PACKAGES_DIR}/cuttlefish-user_*_amd64.deb ${MOUNT_POINT}/tmp/
cp ${PACKAGES_DIR}/cuttlefish-orchestration_*_amd64.deb ${MOUNT_POINT}/tmp/

sudo chroot /mnt/image mkdir /run/resolvconf
sudo cp /etc/resolv.conf /mnt/image/run/resolvconf/resolv.conf

sudo chroot ${MOUNT_POINT} apt update
sudo chroot ${MOUNT_POINT} bash -c 'apt install -y /tmp/cuttlefish-base_*_amd64.deb'
sudo chroot ${MOUNT_POINT} bash -c 'apt install -y /tmp/cuttlefish-user_*_amd64.deb'
sudo chroot ${MOUNT_POINT} bash -c 'apt install -y /tmp/cuttlefish-orchestration_*_amd64.deb'

sudo rm -r ${MOUNT_POINT}/run/resolvconf

sudo umount ${MOUNT_POINT}
sudo rm -r ${MOUNT_POINT}
