#!/usr/bin/env bash

# Copyright (C) 2025 The Android Open Source Project
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

set -o errexit -o nounset -o pipefail

if [[ $# -eq 0 ]] ; then
  echo "usage: $0 <linux-image-deb>"
  exit 1
fi
linux_image_deb=$1

arch=$(uname -m)
[ "${arch}" = "x86_64" ] && arch=amd64
[ "${arch}" = "aarch64" ] && arch=arm64

sudo apt-get update
sudo apt-get upgrade -y

version=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2 )
echo "START VERSION: ${version}"

sudo chroot /mnt/image /usr/bin/apt-get update
sudo chroot /mnt/image /usr/bin/apt-get upgrade -y

version=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2 )
echo "AFTER UPGRADE VERSION: ${version}"

sudo chroot /mnt/image /usr/bin/apt-get install -y ${linux_image_deb}

version=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2)
echo "END VERSION: ${version}"

if [ "${version}" != "${linux_image_deb}" ]; then
  echo "CREATE IMAGE FAILED!!!"
  echo "Expected ${linux_image_deb}, got: ${version}"
  exit 1
fi

# Skip unmounting:
#  Sometimes systemd starts, making it hard to unmount
#  In any case we'll unmount cleanly when the instance shuts down
