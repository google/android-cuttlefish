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

set -o xtrace
# --- begin runfiles.bash initialization v3 ---
# Copy-pasted from the Bazel Bash runfiles library v3.
set -uo pipefail; set +e; f=bazel_tools/tools/bash/runfiles/runfiles.bash
# shellcheck disable=SC1090
source "${RUNFILES_DIR:-/dev/null}/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -f2- -d' ')" 2>/dev/null || \
  source "$0.runfiles/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null || \
  { echo>&2 "ERROR: cannot find $f"; exit 1; }; f=; set -e
# --- end runfiles.bash initialization v3 ---

usage() {
  echo "usage: $0 -o <output>"
  echo "  -o: path for tar format output"
}

output=
while getopts ":ho:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    o)
      output="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      usage
      exit 1
      ;;
  esac
done

script_location="$(rlocation images/image-build-bazel-wrapper.sh)"
repo_root_dir=$(dirname $(dirname $(readlink -f ${script_location})))

head_commit_sha=$(git -C ${repo_root_dir} rev-parse HEAD | cut -c1-8)
name="cuttlefish-orchestration:${head_commit_sha}"

function remove_image() {
  docker rmi ${name} 
}

trap remove_image EXIT

# Build docker image
${repo_root_dir}/docker/image-builder.sh -t "${name}" -d

docker save --output ${output} ${name} 
