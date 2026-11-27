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

# Installs an NVIDIA Tesla driver with open kernel modules (kernel-open), or
# checks whether its open kernel modules build against a candidate kernel.
#
# Usage:
#   install_nvidia.sh --nvidia-version <ver> [--runfile-url <url>] [--check <kernel-version>]

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

nvidia_version=""
runfile_url=""
check_only=false
kmodver=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --nvidia-version)
      nvidia_version=${2:-}
      shift 2 || die "$1 requires a value"
      ;;
    --runfile-url)
      runfile_url=${2:-}
      shift 2 || die "$1 requires a value"
      ;;
    --check)
      check_only=true
      kmodver=${2:-}
      shift 2 || die "$1 requires a value"
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

# Validate arguments upfront before performing any system changes.
if [[ ! "${nvidia_version}" =~ ^[0-9]+[.][0-9]+([.][0-9]+)?$ ]]; then
  die "Invalid or missing --nvidia-version: ${nvidia_version:-<empty>} (expected e.g. 580.196.11)"
fi
if [[ -z "${runfile_url}" ]]; then
  runfile_url="https://us.download.nvidia.com/tesla/${nvidia_version}/NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run"
elif [[ ! "${runfile_url}" =~ ^https://[^[:space:]]+$ ]]; then
  die "Invalid --runfile-url: ${runfile_url} (expected https:// URL without whitespace)"
fi
if [[ "${check_only}" == "true" ]]; then
  if [[ ! "${kmodver}" =~ ^[0-9]+[.][0-9]+[.][0-9]+ ]]; then
    die "Invalid --check kernel version: ${kmodver:-<empty>}"
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

nvidia_installer_dir="NVIDIA-Linux-${nvidia_arch}-${nvidia_version}"
nvidia_runfile="${nvidia_installer_dir}.run"

# A check runs once per candidate kernel. Download the driver only once.
if [[ ! -d "${nvidia_installer_dir}" ]]; then
  wget -q -O "${nvidia_runfile}" "${runfile_url}"
  chmod a+x "${nvidia_runfile}"
  "./${nvidia_runfile}" -x --target "${nvidia_installer_dir}"
fi

module_source_dir="${nvidia_installer_dir}/kernel-open"
if [[ ! -d "${module_source_dir}" ]]; then
  die "NVIDIA driver ${nvidia_version} does not include open kernel modules (${module_source_dir} not found)"
fi

if [[ "${check_only}" == "true" ]]; then
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
    --kernel-module-type=open \
    --silent \
    --no-backup \
    --no-wine-files \
    --install-libglvnd \
    --dkms \
    -k "${kmodver}"
)

rm -rf "${nvidia_installer_dir}" "${nvidia_runfile}"
