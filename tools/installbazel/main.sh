#!/bin/bash

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

function install_bazel() {
  echo "Installing bazel"
  sudo apt install apt-transport-https curl gnupg -y
  curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor >bazel-archive-keyring.gpg
  sudo mv bazel-archive-keyring.gpg /usr/share/keyrings
  echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/sources.list.d/bazel.list
  # bazel needs the zip command to gather test outputs but doesn't depend on it
  sudo apt-get update && sudo apt-get install -y bazel zip unzip
}

install_bazel
