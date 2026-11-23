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

# Install bazel via bazelisk (https://github.com/bazelbuild/bazelisk)

set -e

if command -v bazel &> /dev/null; then
  echo "bazel already installed"
  exit 0
fi

BAZELISK_VERSION=v1.29.0

ARCH=$(uname -m)
if [ "${ARCH}" = "x86_64" ]; then
  ARCH="amd64"
  SHA256="186d78a20e1a64f59ba08987791a989892d142c9d3a9f9cc0c5c35e201f53924"
elif [ "${ARCH}" = "aarch64" ]; then
  ARCH="arm64"
  SHA256="db8ada89c841afd2cb33db7d13aa98ea4fb14612579a8bae84722250caa84272"
else
  echo "Unsupported architecture: ${ARCH}" >&2
  exit 1
fi

apt install -y build-essential file unzip wget zip
tmpdir="$(mktemp -t -d bazel_installer_XXXXXX)"
trap "rm -rf $tmpdir" EXIT
pushd "${tmpdir}"
wget "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-${ARCH}.deb"
echo "${SHA256}  bazelisk-${ARCH}.deb" | sha256sum -c -
apt install -y "./bazelisk-${ARCH}.deb"
popd
