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

die() {
  echo "ERROR: $*" >&2
  exit 1
}

nvidia_arch=$(uname -m)
case "${nvidia_arch}" in
  x86_64)
    arch="amd64"
    other_arch="arm64"
    ;;
  aarch64)
    arch="arm64"
    other_arch="amd64"
    ;;
  *)
    die "Unsupported architecture: ${nvidia_arch} (expected x86_64 or aarch64)"
    ;;
esac

# usage: $0 [--check <kernel version>]
#
# Without arguments, installs the NVIDIA driver for the installed kernel.
#
# With --check, only tests that the driver's kernel modules build against the
# given kernel version (e.g. 6.12.118+deb13-cloud-amd64) and exits. The driver
# is not installed.
check_only=false
kmodver=""
if [[ $# -gt 0 ]]; then
  if [[ "$1" != "--check" || $# -ne 2 ]]; then
    die "Usage: $0 [--check <kernel-version>]"
  fi
  check_only=true
  kmodver=$2
  if [[ ! "${kmodver}" =~ ^[0-9]+[.][0-9]+[.][0-9]+ ]]; then
    die "Invalid --check kernel version: ${kmodver}"
  fi
  if [[ "${kmodver}" == *"-${other_arch}" ]]; then
    die "Kernel version ${kmodver} targets ${other_arch} on a ${arch} host"
  fi
fi

set -x

# NVIDIA driver needs dkms which requires /dev/fd
if [ ! -d /dev/fd ]; then
  ln -s /proc/self/fd /dev/fd
fi

if [[ "${check_only}" == "false" ]]; then
  # Query the installed concrete kernel package version on disk rather than the metapackage
  kmodver=$(dpkg -l | grep '^ii' | awk '{print $2}' | \
            grep '^linux-image-[0-9]' | head -n1 | sed 's/linux-image-//')
  if [[ -z "${kmodver}" ]]; then
    die "No installed concrete linux-image-* kernel package found"
  fi
fi

apt-get install -y wget

# A check only compiles the kernel modules, so it skips the recommended
# packages of the dependencies. Those can include another kernel.
recommends_flag=""
if [[ "${check_only}" == "true" ]]; then
  recommends_flag="--no-install-recommends"
fi

# Dependencies for nvidia-installer
apt-get install -y ${recommends_flag} \
  "linux-headers-${kmodver}" \
  dkms \
  libglvnd-dev \
  libc6-dev \
  pkg-config

nvidia_version=580.196.07
nvidia_installer_dir=NVIDIA-Linux-${nvidia_arch}-${nvidia_version}

# A check runs once per candidate kernel. Download the driver only once.
if [[ ! -d "${nvidia_installer_dir}" ]]; then
  wget -q https://us.download.nvidia.com/tesla/${nvidia_version}/NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run
  chmod a+x NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run
  ./NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run -x
fi

if [[ "${check_only}" == "true" ]]; then
  # The driver ships two sets of kernel modules: proprietary ones in "kernel"
  # and open-source ones in "kernel-open". Build the set that nvidia-installer
  # would install on this machine.
  module_type=$("${nvidia_installer_dir}/nvidia-installer" --print-recommended-kernel-module-type)
  if [[ "${module_type}" == "open" ]]; then
    module_source_dir=${nvidia_installer_dir}/kernel-open
  else
    module_source_dir=${nvidia_installer_dir}/kernel
  fi

  # These are the commands DKMS runs to build the modules for a kernel.
  make -C "${module_source_dir}" KERNEL_UNAME="${kmodver}" clean
  make -C "${module_source_dir}" -j"$(nproc)" KERNEL_UNAME="${kmodver}" modules
  exit 0
fi

arch_specific_flags=""
if [[ "${nvidia_arch}" = "x86_64" ]]; then
  arch_specific_flags="--no-install-compat32-libs"
fi
# nvidia-installer looks up bundled files relative to $PWD.
(
  cd "${nvidia_installer_dir}"
  ./nvidia-installer \
    ${arch_specific_flags} \
    --silent \
    --no-backup \
    --no-wine-files \
    --install-libglvnd \
    --dkms \
    -k "${kmodver}"
)

rm -rf "${nvidia_installer_dir}" "${nvidia_installer_dir}.run"
