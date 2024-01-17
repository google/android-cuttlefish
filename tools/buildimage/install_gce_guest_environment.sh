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

# Install GCP guest environment: https://cloud.google.com/compute/docs/images/install-guest-environment

# IMPORTANT!!! This script expects debian-11-genericcloud-amd64-20240104-1616.raw image.
# https://cloud.debian.org/images/cloud/bullseye/20240104-1616/debian-11-genericcloud-amd64-20240104-1616.raw

usage() {
  echo "usage: $0 -r /path/to/disk.raw"
  exit 1
}

diskraw=

while getopts ":hr:" opt; do
  case "${opt}" in
    h)
      usage
      ;;
    r)
      diskraw="${OPTARG}"
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

sudo chroot /mnt/image mkdir /run/resolvconf
sudo cp /etc/resolv.conf /mnt/image/run/resolvconf/resolv.conf

cat <<'EOF' >${mount_point}/tmp/install.sh
#!/bin/bash
echo "== Installing Google guest environment for Debian =="
export DEBIAN_FRONTEND=noninteractive
echo "Determining Debian version..."
eval $(grep VERSION_CODENAME /etc/os-release)
if [[ -z $VERSION_CODENAME ]]; then
 echo "ERROR: Could not determine Debian version."
 exit 1
fi
echo "Running apt update..."
apt update
echo "Installing gnupg..."
apt install -y gnupg
echo "Adding GPG key for Google cloud repo."
curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | apt-key add -
echo "Updating repo file..."
tee "/etc/apt/sources.list.d/google-cloud.list" << EOM
deb http://packages.cloud.google.com/apt google-compute-engine-${VERSION_CODENAME}-stable main
deb http://packages.cloud.google.com/apt google-cloud-packages-archive-keyring-${VERSION_CODENAME} main
EOM
echo "Running apt update..."
apt update
echo "Installing packages..."
for pkg in google-cloud-packages-archive-keyring google-compute-engine; do
 echo "Running apt install ${pkg}..."
 apt install -y ${pkg}
 if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to install ${pkg}."
 fi
done
EOF
sudo chroot /mnt/image bash /tmp/install.sh

sudo rm -r ${mount_point}/run/resolvconf

sudo umount ${mount_point}
sudo rm -r ${mount_point}
