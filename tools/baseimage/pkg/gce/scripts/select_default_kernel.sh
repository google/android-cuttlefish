#!/usr/bin/env bash

# Copyright (C) 2026 The Android Open Source Project
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

# Prints the newest concrete cloud kernel package in /mnt/image (backports
# included) that the NVIDIA driver builds against. Logs go to stderr so stdout
# only contains the chosen package name.

arch=$(uname -m)
[ "${arch}" = "x86_64" ] && arch=amd64
[ "${arch}" = "aarch64" ] && arch=arm64

list_installed_packages() {
  sudo chroot /mnt/image /usr/bin/dpkg -l \
    | grep '^ii' \
    | awk '{print $2}' \
    | sort
}

apt_candidate_version() {
  local package=$1
  sudo chroot /mnt/image /usr/bin/apt-cache policy "${package}" \
    | awk '$1 == "Candidate:" {print $2; exit}'
}

# Lists concrete cloud kernels (three-part version, skipping metapackages), newest first.
list_kernel_packages_newest_first() {
  local name_pattern="^linux-image-[0-9]+[.][0-9]+[.][0-9]+.*-cloud-${arch}$"
  local packages
  packages=$(sudo chroot /mnt/image /usr/bin/apt-cache search --names-only "${name_pattern}" | awk '{print $1}')

  local package version
  for package in ${packages}; do
    version=$(apt_candidate_version "${package}")
    if [[ -z "${version}" || "${version}" == "(none)" ]]; then
      continue
    fi
    echo "${version} ${package}"
  done \
    | sort --reverse --version-sort \
    | awk '{print $2}'
}

nvidia_driver_builds_on() {
  local kernel_version=$1
  sudo chroot /mnt/image /usr/bin/bash install_nvidia.sh --check "${kernel_version}" >&2
}

purge_packages_installed_since() {
  local packages_before=$1
  local packages_now new_packages
  packages_now=$(list_installed_packages)
  new_packages=$(comm -13 <(echo "${packages_before}") <(echo "${packages_now}"))
  if [[ -n "${new_packages}" ]]; then
    sudo chroot /mnt/image /usr/bin/apt-get purge -y ${new_packages} >&2
  fi
}

# Snapshot installed packages so all temporary check dependencies can be purged.
packages_before_checks=$(list_installed_packages)
sudo cp install_nvidia.sh /mnt/image/

selected_kernel=""
kernel_packages=$(list_kernel_packages_newest_first)
for kernel_package in ${kernel_packages}; do
  kernel_version=${kernel_package#linux-image-}
  if nvidia_driver_builds_on "${kernel_version}"; then
    selected_kernel=${kernel_package}
    break
  fi
  echo "NVIDIA DRIVER DOES NOT BUILD ON: ${kernel_version}" >&2
  sudo chroot /mnt/image /usr/bin/apt-get purge -y --autoremove "linux-headers-${kernel_version}" >&2 || true
done

purge_packages_installed_since "${packages_before_checks}"
sudo rm -rf /mnt/image/install_nvidia.sh /mnt/image/NVIDIA-Linux-*

if [[ -z "${selected_kernel}" ]]; then
  echo "CREATE IMAGE FAILED!!!" >&2
  echo "The NVIDIA driver does not build against any available kernel" >&2
  exit 1
fi

echo "${selected_kernel}"
