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

BAZELISK_VERSION=v1.29.0

ARCH=$(uname -m)
if [ "${ARCH}" = "x86_64" ]; then
  ARCH="amd64"
elif [ "${ARCH}" = "aarch64" ]; then
  ARCH="arm64"
fi

apt install -y build-essential file unzip wget zip
tmpdir="$(mktemp -t -d bazel_installer_XXXXXX)"
trap "rm -rf $tmpdir" EXIT
pushd "${tmpdir}"
wget "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-${ARCH}.deb"
apt install -y "./bazelisk-${ARCH}.deb"
popd
