#!/bin/bash

# Copyright 2018 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -x
set -o errexit
shopt -s extglob

# If "true" install host orchestration capabilities.
host_orchestration_flag="false"

while getopts ":o" flag; do
    case "${flag}" in
        o) host_orchestration_flag="true";;
    esac
done

sudo apt-get update

# Stuff we need to get build support

sudo apt install -y debhelper ubuntu-dev-tools equivs "${extra_packages[@]}"

# Install the cuttlefish build deps

for dsc in *.dsc; do
  yes | sudo mk-build-deps -i "${dsc}" -t apt-get
done

# Installing the build dependencies left some .deb files around. Remove them
# to keep them from landing on the image.
yes | rm -f *.deb

for dsc in *.dsc; do
  # Unpack the source and build it

  dpkg-source -x "${dsc}"
  dir="$(basename "${dsc}" .dsc)"
  dir="${dir/_/-}"
  pushd "${dir}/"
  debuild -uc -us
  popd
done

# Now gather all of the relevant .deb files to copy them into the image
debs=()
if [[ "${host_orchestration_flag}" == "true" ]]; then
  debs=(!(cuttlefish-@(common|user)*).deb)
else
  debs=(!(cuttlefish-orchestration*).deb)
fi

tmp_debs=()
for i in "${debs[@]}"; do
  tmp_debs+=(/tmp/"$(basename "$i")")
done

# Now install the packages on the disk
sudo mkdir -p /mnt/image
sudo mount /dev/sdb1 /mnt/image
cp "${debs[@]}" /mnt/image/tmp
sudo mount -t sysfs none /mnt/image/sys
sudo mount -t proc none /mnt/image/proc
sudo mount --bind /boot/efi /mnt/image/boot/efi
sudo mount --bind /dev/ /mnt/image/dev
sudo mount --bind /dev/pts /mnt/image/dev/pts
sudo mount --bind /run /mnt/image/run
# resolv.conf is needed on Debian but not Ubuntu
sudo cp /etc/resolv.conf /mnt/image/etc/
sudo chroot /mnt/image /usr/bin/apt update
sudo chroot /mnt/image /usr/bin/apt install -y "${tmp_debs[@]}"
# install tools dependencies
sudo chroot /mnt/image /usr/bin/apt install -y openjdk-11-jre
sudo chroot /mnt/image /usr/bin/apt install -y unzip bzip2 lzop
sudo chroot /mnt/image /usr/bin/apt install -y aapt
sudo chroot /mnt/image /usr/bin/apt install -y screen # needed by tradefed

sudo chroot /mnt/image /usr/bin/find /home -ls
sudo chroot /mnt/image /usr/bin/apt install -t bullseye-backports -y linux-image-cloud-amd64

# update QEMU version to most recent backport
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-x86 -t bullseye-backports
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-arm -t bullseye-backports

# Install GPU driver dependencies
sudo cp install_nvidia.sh /mnt/image/
sudo chroot /mnt/image /usr/bin/bash install_nvidia.sh
sudo rm /mnt/image/install_nvidia.sh

# Verify
query_nvidia() {
  sudo chroot /mnt/image nvidia-smi --format=csv,noheader --query-gpu="$@"
}

if [[ $(query_nvidia "count") != "1" ]]; then
  echo "Failed to detect GPU."
  exit 1
fi

if [[ $(query_nvidia "driver_version") == "" ]]; then
  echo "Failed to detect GPU driver."
  exit 1
fi

# Vulkan loader
sudo chroot /mnt/image /usr/bin/apt install -y libvulkan1 -t bullseye-backports

# Wayland-server needed to have Nvidia driver fail gracefully when attemping to
# use the EGL API on GCE instances without a GPU.
sudo chroot /mnt/image /usr/bin/apt install -y libwayland-server0 -t bullseye-backports

# Clean up the builder's version of resolv.conf
sudo rm /mnt/image/etc/resolv.conf

# Make sure the image has /var/empty, and allow unprivileged_userns_clone for
# minijail process sandboxing
sudo chroot /mnt/image /usr/bin/mkdir -p /var/empty
sudo tee /mnt/image/etc/sysctl.d/80-nsjail.conf >/dev/null <<EOF
kernel.unprivileged_userns_clone=1
EOF

# Skip unmounting:
#  Sometimes systemd starts, making it hard to unmount
#  In any case we'll unmount cleanly when the instance shuts down

echo IMAGE_WAS_CREATED
