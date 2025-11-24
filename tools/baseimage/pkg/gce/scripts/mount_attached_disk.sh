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

# Mount attached disk `/dev/sdb1` at `/mnt/image`.
set -o errexit -o nounset -o pipefail

sudo mkdir -p /mnt/image
sudo mount /dev/sdb1 /mnt/image
sudo mount -t sysfs none /mnt/image/sys
sudo mount -t proc none /mnt/image/proc
sudo mount --bind /boot/efi /mnt/image/boot/efi
sudo mount --bind /dev/ /mnt/image/dev
sudo mount --bind /dev/pts /mnt/image/dev/pts
sudo mount --bind /run /mnt/image/run
# resolv.conf is needed on Debian but not Ubuntu
if [ ! -f /mnt/image/etc/resolv.conf ]; then
  sudo cp /etc/resolv.conf /mnt/image/etc/
fi
`
