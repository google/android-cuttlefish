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

# Install bazel (https://bazel.build/install/ubuntu)

set -e

function install_bazel_x86_64() {
  echo "Installing bazel"
  apt install apt-transport-https curl gnupg -y
  curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor >bazel-archive-keyring.gpg
  mv bazel-archive-keyring.gpg /usr/share/keyrings
  echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list
  # bazel needs the zip command to gather test outputs but doesn't depend on it
  apt-get update && apt-get install -y bazel zip unzip
}

function install_bazel_aarch64() {
  BAZELISK_VERSION=v1.19.0
  apt install wget
  tmpdir="$(mktemp -t -d bazel_installer_XXXXXX)"
  trap "rm -rf $tmpdir" EXIT
  pushd "${tmpdir}"
  wget "https://github.com/bazelbuild/bazelisk/releases/download/${BAZELISK_VERSION}/bazelisk-linux-arm64"
  mv bazelisk-linux-arm64 /usr/local/bin/bazel
  chmod 0755 /usr/local/bin/bazel
  popd
}

<<<<<<< HEAD
=======
function install_bazel_riscv64() {
  # No official Bazel binary exists for riscv64. Bazel must be built from
  # source (~30-60 minutes).
  echo "error: bazel is not installed and cannot be installed automatically on riscv64." >&2
  echo "error: bazel must be built from source. Run the following script then re-run build_packages.sh:" >&2
  cat >&2 << 'BUILD_SCRIPT'

#!/usr/bin/env bash
set -e
BAZEL_VERSION=8.5.1
sudo apt-get install -y build-essential openjdk-21-jdk python3 zip unzip
mkdir bazel-${BAZEL_VERSION} && cd bazel-${BAZEL_VERSION}
wget https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-dist.zip
unzip bazel-${BAZEL_VERSION}-dist.zip
env EXTRA_BAZEL_ARGS="--tool_java_runtime_version=local_jdk" bash ./compile.sh
sudo cp output/bazel /usr/local/bin/bazel

BUILD_SCRIPT
  exit 1
}

>>>>>>> 826e01940 (installbazel: add riscv64 support with build-from-source instructions)
install_bazel_$(uname -m)
