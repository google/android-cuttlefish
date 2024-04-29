#!/bin/bash

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

set -e

usage() {
  echo "usage: $0 -r /path/to/disk.raw -p /path/to/packages_dir"
  exit 1
}

diskraw=
packages_dir=

while getopts ":hr:p:" opt; do
  case "${opt}" in
    h)
      usage
      ;;
    r)
      diskraw="${OPTARG}"
      ;;
    p)
      packages_dir="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      usage
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      usage
      ;;
  esac
done

mount_point="/mnt/image"
sudo mkdir ${mount_point}
# offset value is 262144 * 512, the `262144`th is the sector where the `Linux filesystem` partition
# starts and `512` bytes is the sectors size. See `sudo fdisk -l disk.raw`.
sudo mount -o loop,offset=$((262144 * 512)) ${diskraw} ${mount_point}

cp ${packages_dir}/cuttlefish-base_*_amd64.deb ${mount_point}/tmp/
cp ${packages_dir}/cuttlefish-user_*_amd64.deb ${mount_point}/tmp/
cp ${packages_dir}/cuttlefish-orchestration_*_amd64.deb ${mount_point}/tmp/

sudo chroot /mnt/image mkdir /run/resolvconf
sudo cp /etc/resolv.conf /mnt/image/run/resolvconf/resolv.conf

sudo chroot ${mount_point} apt update
sudo chroot ${mount_point} bash -c 'apt install -y /tmp/cuttlefish-base_*_amd64.deb'
sudo chroot ${mount_point} bash -c 'apt install -y /tmp/cuttlefish-user_*_amd64.deb'
sudo chroot ${mount_point} bash -c 'apt install -y /tmp/cuttlefish-orchestration_*_amd64.deb'

sudo rm -r ${mount_point}/run/resolvconf

sudo umount ${mount_point}
sudo rm -r ${mount_point}
