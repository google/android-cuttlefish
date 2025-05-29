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

# Install GCP guest environment: https://cloud.google.com/compute/docs/images/install-guest-environment

# IMPORTANT!!! This script expects debian-11-genericcloud-amd64-20240104-1616.raw image.
# https://cloud.debian.org/images/cloud/bullseye/20240104-1616/debian-11-genericcloud-amd64-20240104-1616.raw

set -o errexit -o nounset -o pipefail

function print_usage() {
  >&2 echo "usage: $0 -r /path/to/disk.raw"
}

DISK_RAW=

while getopts ":hr:" opt; do
  case "${opt}" in
    h)
      print_usage
      ;;
    r)
      DISK_RAW="${OPTARG}"
      ;;
    *)
      >&2 echo "Invalid option: -${OPTARG}"
      print_usage
      exit 1
      ;;
  esac
done

readonly DISK_RAW

if [[ "${DISK_RAW}" == "" ]]; then
  echo "path to disk raw is required"
  print_usage
  exit 1
fi

readonly MOUNT_POINT="/mnt/image"

function cleanup() {
  sudo rm -rf ${MOUNT_POINT}/run/resolvconf

  sudo umount -f ${MOUNT_POINT}/dev
  sudo umount -f ${MOUNT_POINT} && sudo rm -r ${MOUNT_POINT}
}

trap cleanup EXIT

sudo mkdir ${MOUNT_POINT}
# offset value is 262144 * 512, the `262144`th is the sector where the `Linux filesystem` partition
# starts and `512` bytes is the sectors size. See `sudo fdisk -l disk.raw`.
sudo mount -o loop,offset=$((262144 * 512)) ${DISK_RAW} ${MOUNT_POINT}
sudo mount --bind /dev/ ${MOUNT_POINT}/dev

sudo chroot ${MOUNT_POINT} mkdir /run/resolvconf
sudo cp /etc/resolv.conf ${MOUNT_POINT}/run/resolvconf/resolv.conf

cat <<'EOF' >${MOUNT_POINT}/tmp/install.sh
#!/usr/bin/env bash
apt-get update && apt-get install -y gnupg2
curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | apt-key add -
eval $(grep VERSION_CODENAME /etc/os-release)
echo "${VERSION_CODENAME}"
tee /etc/apt/sources.list.d/google-cloud.list << EOM
deb http://packages.cloud.google.com/apt google-compute-engine-${VERSION_CODENAME}-stable main
deb http://packages.cloud.google.com/apt google-cloud-packages-archive-keyring-${VERSION_CODENAME} main
EOM
apt update
apt install -y google-cloud-packages-archive-keyring
apt install -y google-compute-engine google-osconfig-agent
EOF

sudo chroot /mnt/image bash /tmp/install.sh
