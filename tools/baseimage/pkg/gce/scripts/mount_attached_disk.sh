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

# Mount attached disk `/dev/sdb1` at `/mnt/image` (the argument).
set -o errexit -o nounset -o pipefail

MOUNTPONT=$1

sudo mkdir -p ${MOUNTPOINT}
sudo mount /dev/sdb1 ${MOUNTPOINT}
sudo mount -t sysfs none ${MOUNTPOINT}/sys
sudo mount -t proc none ${MOUNTPOINT}/proc
sudo mount --bind /boot/efi ${MOUNTPOINT}/boot/efi
sudo mount --bind /dev/ ${MOUNTPOINT}/dev
sudo mount --bind /dev/pts ${MOUNTPOINT}/dev/pts
sudo mount --bind /run ${MOUNTPOINT}/run
# resolv.conf is needed on Debian but not Ubuntu
if [ ! -f ${MOUNTPOINT}/etc/resolv.conf ]; then
  sudo cp /etc/resolv.conf ${MOUNTPOINT}/etc/
fi
`
