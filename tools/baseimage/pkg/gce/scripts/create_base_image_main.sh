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

arch=$(uname -m)
[ "${arch}" = "x86_64" ] && arch=amd64
[ "${arch}" = "aarch64" ] && arch=arm64

sudo apt-get update
sudo apt-get upgrade -y

# Avoids blocking "Default mirror not found" popup prompt when pbuilder is installed.
echo "pbuilder        pbuilder/mirrorsite     string  https://deb.debian.org/debian" | sudo debconf-set-selections

kmodver_begin=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2 | sed 's/linux-image-//')
echo "IMAGE STARTS WITH KERNEL: ${kmodver_begin}"

sudo chroot /mnt/image /usr/bin/apt update
sudo chroot /mnt/image /usr/bin/apt upgrade -y

# Disable systemd mounting tmpfs at /tmp due backwards compatibility issues.
# TODO(b/458388172): Remove line if cvd no longer stores artifacts
# in /tmp by default.
sudo chroot /mnt/image /usr/bin/systemctl mask tmp.mount

# Avoid automatic updates during tests.
# https://manpages.debian.org/trixie/unattended-upgrades/unattended-upgrade.8.en.html
sudo chroot /mnt/image /usr/bin/apt purge -y unattended-upgrades

# Install JDK.
#
# JDK it's not required to launch a CF device. It's required to run
# some of Tradefed tests that are run from the CF host side like
# some CF gfx tests, adb tests, etc.
if [[ "${arch}" == "amd64" ]]; then
  JDK_ARCH=x64
  # https://download.java.net/java/GA/jdk21.0.2/f2283984656d49d69e91c558476027ac/13/GPL/openjdk-21.0.2_linux-x64_bin.tar.gz.sha256
  export JDK21_SHA256SUM=a2def047a73941e01a73739f92755f86b895811afb1f91243db214cff5bdac3f
elif [[ "${arch}" == "arm64" ]]; then
  JDK_ARCH=aarch64
  # https://download.java.net/java/GA/jdk21.0.2/f2283984656d49d69e91c558476027ac/13/GPL/openjdk-21.0.2_linux-aarch64_bin.tar.gz.sha256
  export JDK21_SHA256SUM=08db1392a48d4eb5ea5315cf8f18b89dbaf36cda663ba882cf03c704c9257ec2
else
  echo "** ERROR: UNEXEPCTED ARCH **"; exit 1;
fi
sudo chroot /mnt/image /usr/bin/wget -P /usr/java "https://download.java.net/java/GA/jdk21.0.2/f2283984656d49d69e91c558476027ac/13/GPL/openjdk-21.0.2_linux-${JDK_ARCH}_bin.tar.gz"
if ! echo "$JDK21_SHA256SUM /usr/java/openjdk-21.0.2_linux-${JDK_ARCH}_bin.tar.gz" | sudo chroot /mnt/image /usr/bin/sha256sum -c ; then
  echo "** ERROR: KEY MISMATCH **"; popd >/dev/null; exit 1;
fi
sudo chroot /mnt/image /usr/bin/tar xvzf "/usr/java/openjdk-21.0.2_linux-${JDK_ARCH}_bin.tar.gz" -C /usr/java
sudo chroot /mnt/image /usr/bin/rm "/usr/java/openjdk-21.0.2_linux-${JDK_ARCH}_bin.tar.gz"
ENV_JAVA_HOME='/usr/java/jdk-21.0.2'
echo "JAVA_HOME=$ENV_JAVA_HOME" | sudo chroot /mnt/image /usr/bin/tee -a /etc/environment >/dev/null
echo "JAVA_HOME=$ENV_JAVA_HOME" | sudo chroot /mnt/image /usr/bin/tee -a /etc/profile >/dev/null
echo 'PATH=$JAVA_HOME/bin:$PATH' | sudo chroot /mnt/image /usr/bin/tee -a /etc/profile >/dev/null
echo "PATH=$ENV_JAVA_HOME/bin:/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games" | sudo chroot /mnt/image /usr/bin/tee -a /etc/environment >/dev/null

# install tools dependencies
sudo chroot /mnt/image /usr/bin/apt install -y unzip bzip2 lzop
sudo chroot /mnt/image /usr/bin/apt install -y aapt
sudo chroot /mnt/image /usr/bin/apt install -y adb # needed by tradefed
sudo chroot /mnt/image /usr/bin/apt install -y screen # needed by tradefed

sudo chroot /mnt/image /usr/bin/find /home -ls

# update QEMU version to most recent backport
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-x86
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-arm
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-misc

# Install GPU driver dependencies
sudo cp install_nvidia.sh /mnt/image/
sudo chroot /mnt/image /usr/bin/bash install_nvidia.sh
sudo rm /mnt/image/install_nvidia.sh

# Vulkan loader
sudo chroot /mnt/image /usr/bin/apt install -y libvulkan1

# Wayland-server needed to have Nvidia driver fail gracefully when attempting to
# use the EGL API on GCE instances without a GPU.
sudo chroot /mnt/image /usr/bin/apt install -y libwayland-server0

# Clean up the builder's version of resolv.conf
sudo rm /mnt/image/etc/resolv.conf

# Make sure the image has /var/empty, and allow unprivileged_userns_clone for
# minijail process sandboxing
sudo chroot /mnt/image /usr/bin/mkdir -p /var/empty
sudo tee /mnt/image/etc/sysctl.d/80-nsjail.conf >/dev/null <<EOF
kernel.unprivileged_userns_clone=1
EOF

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
