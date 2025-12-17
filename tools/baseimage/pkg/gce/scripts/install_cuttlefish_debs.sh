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
  echo "usage: $0 /path/to/deb1 /path/to/deb2 /path/to/deb3"
  exit 1
fi

arch=$(uname -m)
[ "${arch}" = "x86_64" ] && arch=amd64
[ "${arch}" = "aarch64" ] && arch=arm64

kmodver_begin=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2 | sed 's/linux-image-//')
echo "IMAGE STARTS WITH KERNEL: ${kmodver_begin}"

sudo chroot /mnt/image /usr/bin/apt update
sudo chroot /mnt/image /usr/bin/apt upgrade -y

rm -rf /mnt/image/tmp/install
mkdir /mnt/image/tmp/install

# Install packages
for src in "$@"
do
  echo "Installing: ${src}"
  name=$(basename "${src}")
  cp "${src}" "/mnt/image/tmp/install/${name}"
  sudo chroot /mnt/image /usr/bin/apt install -y "/tmp/install/${name}"
done

kmodver_end=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2 | sed 's/linux-image-//')
echo "IMAGE ENDS WITH KERNEL: ${kmodver_end}"

if [ "${kmodver_begin}" != "${kmodver_end}" ]; then
  echo "KERNEL UPDATE DETECTED!!! ${kmodver_begin} -> ${kmodver_end}"
  echo "Use source image with kernel ${kmodver_end} installed."
  exit 1
fi

# Skip unmounting:
#  Sometimes systemd starts, making it hard to unmount
#  In any case we'll unmount cleanly when the instance shuts down
