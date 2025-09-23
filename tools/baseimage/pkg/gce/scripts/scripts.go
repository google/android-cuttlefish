// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package scripts

// https://cs.android.com/android/platform/superproject/main/+/main:device/google/cuttlefish/tools/update_gce_kernel.sh;drc=7f601ad9132960b58ee3d7fe8f8b382d20720a22
const UpdateKernel = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

sudo apt update
sudo apt install -t bookworm -y linux-image-cloud-amd64
sudo reboot
`

// https://cs.android.com/android/platform/superproject/main/+/main:device/google/cuttlefish/tools/update_gce_kernel.sh;drc=7f601ad9132960b58ee3d7fe8f8b382d20720a22
const RemoveOldKernel = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

dpkg --list | grep -v $(uname -r) | grep -E 'linux-image-[0-9]|linux-headers-[0-9]' | awk '{print $2" "$3}' | sort -k2,2 | awk '{print $1}' | xargs sudo apt-get -y purge
sudo update-grub2
`

// Mount attached disk `/dev/sdb1` at `/mnt/image`.
const MountAttachedDisk = `#!/usr/bin/env bash
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

const CreateBaseImageMain = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

sudo apt-get update

# Avoids blocking "Default mirror not found" popup prompt when pbuilder is installed.
echo "pbuilder        pbuilder/mirrorsite     string  https://deb.debian.org/debian" | sudo debconf-set-selections

# Resize
sudo apt install -y cloud-utils
sudo apt install -y cloud-guest-utils
sudo apt install -y fdisk
sudo growpart /dev/sdb 1 || /bin/true
sudo e2fsck -f -y /dev/sdb1 || /bin/true
sudo resize2fs /dev/sdb1

./mount_attached_disk.sh

sudo chroot /mnt/image /usr/bin/apt update

kmodver_begin=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-amd64 | grep ^Depends: | \
  cut -d: -f2 | cut -d" " -f2 | sed 's/linux-image-//')
echo "IMAGE STARTS WITH KERNEL: ${kmodver_begin}"

# Install JDK.
#
# JDK it's not required to launch a CF device. It's required to run
# some of Tradefed tests that are run from the CF host side like
# some CF gfx tests, adb tests, etc.
sudo chroot /mnt/image /usr/bin/wget -P /usr/java https://download.java.net/java/GA/jdk21.0.2/f2283984656d49d69e91c558476027ac/13/GPL/openjdk-21.0.2_linux-x64_bin.tar.gz
# https://download.java.net/java/GA/jdk21.0.2/f2283984656d49d69e91c558476027ac/13/GPL/openjdk-21.0.2_linux-x64_bin.tar.gz.sha256
export JDK21_SHA256SUM=a2def047a73941e01a73739f92755f86b895811afb1f91243db214cff5bdac3f
if ! echo "$JDK21_SHA256SUM /usr/java/openjdk-21.0.2_linux-x64_bin.tar.gz" | sudo chroot /mnt/image /usr/bin/sha256sum -c ; then
  echo "** ERROR: KEY MISMATCH **"; popd >/dev/null; exit 1;
fi
sudo chroot /mnt/image /usr/bin/tar xvzf /usr/java/openjdk-21.0.2_linux-x64_bin.tar.gz -C /usr/java
sudo chroot /mnt/image /usr/bin/rm /usr/java/openjdk-21.0.2_linux-x64_bin.tar.gz
ENV_JAVA_HOME='/usr/java/jdk-21.0.2'
echo "JAVA_HOME=$ENV_JAVA_HOME" | sudo chroot /mnt/image /usr/bin/tee -a /etc/environment >/dev/null
echo "JAVA_HOME=$ENV_JAVA_HOME" | sudo chroot /mnt/image /usr/bin/tee -a /etc/profile >/dev/null
echo 'PATH=$JAVA_HOME/bin:$PATH' | sudo chroot /mnt/image /usr/bin/tee -a /etc/profile >/dev/null
echo "PATH=$ENV_JAVA_HOME/bin:/usr/local/bin:/usr/bin:/bin:/usr/local/games:/usr/games" | sudo chroot /mnt/image /usr/bin/tee -a /etc/environment >/dev/null

# install tools dependencies
sudo chroot /mnt/image /usr/bin/apt install -y unzip bzip2 lzop
sudo chroot /mnt/image /usr/bin/apt install -y aapt
sudo chroot /mnt/image /usr/bin/apt install -y screen # needed by tradefed

sudo chroot /mnt/image /usr/bin/find /home -ls

# Install image from backports which has matching headers
echo "deb http://deb.debian.org/debian bookworm-backports main" | \
  sudo chroot /mnt/image /usr/bin/tee -a /etc/apt/sources.list >/dev/null

sudo chroot /mnt/image /usr/bin/apt-get update

# update QEMU version to most recent backport
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-x86 -t bookworm
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-arm -t bookworm
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-misc -t bookworm

# Install GPU driver dependencies
sudo cp install_nvidia.sh /mnt/image/
sudo chroot /mnt/image /usr/bin/bash install_nvidia.sh
sudo rm /mnt/image/install_nvidia.sh

# Vulkan loader
sudo chroot /mnt/image /usr/bin/apt install -y libvulkan1 -t bookworm

# Wayland-server needed to have Nvidia driver fail gracefully when attempting to
# use the EGL API on GCE instances without a GPU.
sudo chroot /mnt/image /usr/bin/apt install -y libwayland-server0 -t bookworm

# Clean up the builder's version of resolv.conf
sudo rm /mnt/image/etc/resolv.conf

# Make sure the image has /var/empty, and allow unprivileged_userns_clone for
# minijail process sandboxing
sudo chroot /mnt/image /usr/bin/mkdir -p /var/empty
sudo tee /mnt/image/etc/sysctl.d/80-nsjail.conf >/dev/null <<EOF
kernel.unprivileged_userns_clone=1
EOF

kmodver_end=$(sudo chroot /mnt/image/ /usr/bin/dpkg -s linux-image-cloud-amd64 | grep ^Depends: | \
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
`

const InstallNvidia = `#!/usr/bin/env bash
set -x
set -o errexit

arch=$(uname -m)
nvidia_arch=${arch}
[ "${arch}" = "x86_64" ] && arch=amd64
[ "${arch}" = "aarch64" ] && arch=arm64

# NVIDIA driver needs dkms which requires /dev/fd
if [ ! -d /dev/fd ]; then
  ln -s /proc/self/fd /dev/fd
fi

# Using "Depends:" is more reliable than "Version:", because it works for
# backported ("bpo") kernels as well. NOTE: "Package" can be used instead
# if we don't install the metapackage ("linux-image-cloud-${arch}") but a
# specific version in the future
kmodver=$(dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
          cut -d: -f2 | cut -d" " -f2 | sed 's/linux-image-//')

apt-get install -y wget

# Install headers from backports, to match the linux-image:
apt-get install -y -t bookworm-backports $(echo linux-headers-${kmodver})
# Dependencies for nvidia-installer
apt-get install -y dkms libglvnd-dev libc6-dev pkg-config

nvidia_version=570.158.01

wget -q https://us.download.nvidia.com/tesla/${nvidia_version}/NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run
chmod a+x NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run
./NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run -x
NVIDIA-Linux-${nvidia_arch}-${nvidia_version}/nvidia-installer --silent --no-install-compat32-libs --no-backup --no-wine-files --install-libglvnd --dkms -k "${kmodver}"
`

const InstallCuttlefishPackages = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

if [[ $# -eq 0 ]] ; then
  echo "usage: $0 /path/to/zip"
  exit 1
fi

./mount_attached_disk.sh

sudo chroot /mnt/image /usr/bin/apt update
sudo chroot /mnt/image /usr/bin/apt upgrade -y

# Unzip
sudo apt install -y unzip
rm -rf /mnt/image/tmp/install
unzip -d /mnt/image/tmp/install $1

readonly PKG_NAMES=("cuttlefish-base" "cuttlefish-user" "cuttlefish-orchestration")

# Install packages
for name in "${PKG_NAMES[@]}"; do
  path=("/mnt/image/tmp/install/${name}"*.deb)
  path="${path[0]}"
  name=$(basename "${path}")
  echo "Installing: ${name}"

  sudo chroot /mnt/image /usr/bin/apt install -y "/tmp/install/${name}"
done
`
