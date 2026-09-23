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

function print_usage() {
  >&2 echo "usage: $0 [-i <runner_index>] [-n <runners_total>]"
}

runner_index="1"
runners_total="1"

while getopts ":i:n:" opt; do
  case "${opt}" in
    i)
      runner_index="${OPTARG}"
      ;;
    n)
      runners_total="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      print_usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      print_usage
      exit 1
      ;;
  esac
done

echo "runner_index: ${runner_index}"
echo "runners_total: ${runners_total}"

readonly PKGS_DIR="${KOKORO_GFILE_DIR:-}/github/android-cuttlefish"

debs="$(find "${PKGS_DIR}" -maxdepth 1 -name '*.deb' 2>/dev/null || true)"
if [[ -z "${debs}" ]]; then
  echo "Error: no package found in ${PKGS_DIR}!" >&2
  exit 1
fi
echo "Packages found in ${PKGS_DIR}:"
echo "${debs}"

sudo apt update

# environment variable and options to force answer prompts
sudo DEBIAN_FRONTEND=noninteractive apt -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" upgrade -y

# realpath .kokoro/..
REPO_DIR="$(realpath "$(dirname "$0")"/..)"
TOOL_DIR="${REPO_DIR}/tools"

# Add test user to the kokoro group so it has access to the source dir
"${TOOL_DIR}/testutils/prepare_host.sh" -d "${PKGS_DIR}" -u testrunner -g kokoro

# Allow kokoro group to the source dir:
sudo chmod -R g+w /tmpfs/src

# Install bazel
command -v bazel &> /dev/null || sudo "${TOOL_DIR}/buildutils/installbazel.sh"

# Run as different user without sudo privileges
sudo -u testrunner CREDENTIAL_SOURCE=gce "${TOOL_DIR}/testutils/runcvde2etests_v2.sh" \
  -i ${runner_index} \
  -n ${runners_total}

