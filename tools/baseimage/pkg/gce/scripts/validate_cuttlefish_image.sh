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

sudo apt-get install -y git

# Validate NVIDIA driver installation.
nvidia-smi

dpkg -s cuttlefish-base
cvd_version=$(dpkg -s cuttlefish-base | grep Version: | cut -d" " -f2)
echo "cvd version: ${cvd_version}"
major=$(echo -n "${cvd_version}" | cut -d "." -f1)
minor=$(echo -n "${cvd_version}" | cut -d "." -f2)
branch="version-${major}.${minor}-dev"
echo "running e2e tests from branch ${branch}"
git clone https://github.com/google/android-cuttlefish -b ${branch}
cd android-cuttlefish
sudo bash tools/buildutils/installbazel.sh
cd e2etests
tests=( $(bazel query --noshow_progress 'kind("go_test", orchestration/...) except attr(tags, "[\[ ]host-ready-special[,\]]", //...)' | grep -e "^\/\/" | sort) )
if [ -z "$tests" ]; then
    echo "tests list is empty"
    exit 1
fi
for t in "${tests[@]}"; do
  echo "running test: ${t}"
  bazel test --test_timeout 600 --sandbox_writable_path=$HOME ${t}
  res=$(curl --fail -X POST "http://localhost:2080/reset")
  op_name=$(echo "${res}" | jq -r '.name')
  curl --fail -X POST http://localhost:2080/operations/${op_name}/:wait
done
