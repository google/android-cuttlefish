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

# Usage:
#   install_kernel_main.sh --linux-image-deb <pkg>
#   install_kernel_main.sh --nvidia-version <ver> [--runfile-url <url>]

die() {
  echo "ERROR: $*" >&2
  exit 1
}

host_arch=$(uname -m)
case "${host_arch}" in
  x86_64)
    arch="amd64"
    other_arch="arm64"
    ;;
  aarch64)
    arch="arm64"
    other_arch="amd64"
    ;;
  *)
    die "Unsupported architecture: ${host_arch} (expected x86_64 or aarch64)"
    ;;
esac

linux_image_deb=""
nvidia_version=""
runfile_url=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --linux-image-deb)
      linux_image_deb=${2:-}
      shift 2 || die "$1 requires a value"
      ;;
    --nvidia-version)
      nvidia_version=${2:-}
      shift 2 || die "$1 requires a value"
      ;;
    --runfile-url)
      runfile_url=${2:-}
      shift 2 || die "$1 requires a value"
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

# Validate arguments and combinations upfront before running apt-get.
if [[ ! -d "/mnt/image" ]]; then
  die "Target image mount point /mnt/image does not exist"
fi
if [[ -n "${linux_image_deb}" ]]; then
  if [[ -n "${nvidia_version}" || -n "${runfile_url}" ]]; then
    die "--linux-image-deb cannot be combined with --nvidia-version or --runfile-url"
  fi
  if [[ ! "${linux_image_deb}" =~ ^linux-image-[0-9]+[.][0-9]+[.][0-9]+[a-zA-Z0-9.+~-]*$ ]]; then
    die "Invalid --linux-image-deb: ${linux_image_deb}"
  fi
  if [[ "${linux_image_deb}" == *"-${other_arch}" ]]; then
    die "--linux-image-deb ${linux_image_deb} targets ${other_arch} on a ${arch} host"
  fi
else
  if [[ ! "${nvidia_version}" =~ ^[0-9]+[.][0-9]+([.][0-9]+)?$ ]]; then
    die "Invalid or missing --nvidia-version: ${nvidia_version:-<empty>} (or pass --linux-image-deb)"
  fi
  if [[ -n "${runfile_url}" && ! "${runfile_url}" =~ ^https://[^[:space:]]+$ ]]; then
    die "Invalid --runfile-url: ${runfile_url}"
  fi
  if [[ ! -x "./select_default_kernel.sh" || ! -f "./install_nvidia.sh" ]]; then
    die "Required helper scripts ./select_default_kernel.sh and ./install_nvidia.sh must exist"
  fi
fi

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

list_installed_packages() {
  sudo chroot /mnt/image /usr/bin/dpkg -l \
    | grep '^ii' \
    | awk '{print $2}' \
    | sort
}

# Prints the candidate version apt would install for a package (or "(none)").
apt_candidate_version() {
  local package=$1
  sudo chroot /mnt/image /usr/bin/apt-cache policy "${package}" \
    | awk '$1 == "Candidate:" {print $2; exit}'
}

# Prints the apt suite providing wanted_version of package (e.g. trixie-backports).
apt_suite_of_version() {
  local package=$1
  local wanted_version=$2
  local madison
  madison=$(sudo chroot /mnt/image /usr/bin/apt-cache madison "${package}")

  local version source suite_and_component
  while IFS='|' read -r _ version source; do
    read -r version <<< "${version}"
    if [[ "${version}" != "${wanted_version}" ]]; then
      continue
    fi
    read -r _ suite_and_component _ <<< "${source}"
    echo "${suite_and_component%%/*}"
    return
  done <<< "${madison}"
}

if [[ -z "${linux_image_deb}" ]]; then
  select_args=(--nvidia-version "${nvidia_version}")
  if [[ -n "${runfile_url}" ]]; then
    select_args+=(--runfile-url "${runfile_url}")
  fi
  linux_image_deb=$(./select_default_kernel.sh "${select_args[@]}")
  echo "DEFAULT KERNEL: ${linux_image_deb}"
fi

linux_image_version=$(apt_candidate_version "${linux_image_deb}")
if [[ -z "${linux_image_version}" || "${linux_image_version}" == "(none)" ]]; then
  echo "CREATE IMAGE FAILED!!!"
  echo "Package ${linux_image_deb} is not available in any apt repository"
  exit 1
fi

# Backports kernels depend on newer packages (e.g. linux-base) from backports.
linux_image_suite=$(apt_suite_of_version "${linux_image_deb}" "${linux_image_version}")
if [[ "${linux_image_suite}" == *-backports ]]; then
  echo "TARGET RELEASE: ${linux_image_suite}"
  sudo chroot /mnt/image /usr/bin/apt-get install -y -t "${linux_image_suite}" "${linux_image_deb}"
else
  sudo chroot /mnt/image /usr/bin/apt-get install -y "${linux_image_deb}"
fi

# Purge old kernels and metapackages (including linux-{base,modules}-cloud from
# backports) so only the target kernel remains.
old_kernel_pattern="^linux-image-|^linux-(base|modules)-cloud-${arch}$"
old_kernels=$(list_installed_packages \
  | grep -E "${old_kernel_pattern}" \
  | grep --invert-match --fixed-strings --line-regexp "${linux_image_deb}" \
  || true)
if [ -n "${old_kernels}" ]; then
  echo "Removing old kernel packages: ${old_kernels}"
  sudo chroot /mnt/image /usr/bin/apt-get purge -y ${old_kernels}
  # update-grub may fail in a chroot; the grub config will be rebuilt on boot.
  sudo chroot /mnt/image /bin/sh -c 'command -v update-grub >/dev/null && update-grub || true'
else
  echo "No old kernel packages to remove"
fi

version=$(list_installed_packages | grep '^linux-image-[0-9]' || true)
echo "END VERSION: ${version}"

if [ "${version}" != "${linux_image_deb}" ]; then
  echo "CREATE IMAGE FAILED!!!"
  echo "Expected ${linux_image_deb}, got: ${version:-not found}"
  exit 1
fi

# Skip unmounting:
#  Sometimes systemd starts, making it hard to unmount
#  In any case we'll unmount cleanly when the instance shuts down
