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
  >&2 echo "usage: $0 [-c <config>] [-i <runner_index>] [-n <runners_total>]"
}

readonly CONFIG_PODCVD="podcvd"
readonly CONFIG_PODCVD_WITH_GPU="podcvd_with_gpu"

config="${CONFIG_PODCVD}"
runner_index="1"
runners_total="1"

while getopts ":c:i:n:" opt; do
  case "${opt}" in
    c)
      config="${OPTARG}"
      ;;
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

case "${config}" in
  "${CONFIG_PODCVD}" | "${CONFIG_PODCVD_WITH_GPU}") ;;
  *)
    echo "ERROR: invalid configuration name '${config}'" >&2
    print_usage
    exit 1
    ;;
esac

echo "config: ${config}"
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

# realpath .kokoro/..
REPO_DIR="$(realpath "$(dirname "$0")"/..)"
TOOL_DIR="${REPO_DIR}/tools"

if [[ "${config}" == "${CONFIG_PODCVD_WITH_GPU}" ]]; then
  sudo install -m 0755 -d /etc/apt/keyrings
  sudo curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey -o /etc/apt/keyrings/nvidia-container-toolkit.asc
  sudo curl -sL https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list -o /etc/apt/sources.list.d/nvidia-container-toolkit.list
  sudo sed -i 's#deb https://#deb [signed-by=/etc/apt/keyrings/nvidia-container-toolkit.asc] https://#g' /etc/apt/sources.list.d/nvidia-container-toolkit.list
  # Only refresh the NVIDIA source just added above, leave the rest of the
  # package index untouched.
  sudo apt-get update \
    -o Dir::Etc::sourcelist=/etc/apt/sources.list.d/nvidia-container-toolkit.list \
    -o Dir::Etc::sourceparts=- \
    -o APT::Get::List-Cleanup=0
  sudo apt-get install -y nvidia-container-toolkit
fi

sudo apt-get install -y podman

# Add test user to the kokoro group so it has access to the source dir
"${TOOL_DIR}/testutils/prepare_host_podcvd.sh" -d "${PKGS_DIR}" -u testrunner -g kokoro

# Build container image including cuttlefish-metrics
# image-builder.sh -m dev expects the prebuilt *.deb files in the repo root
cp "${PKGS_DIR}"/*.deb "${REPO_DIR}/"
sudo -u testrunner "${REPO_DIR}/container/image/image-builder.sh" -c podman -m dev -M -t localhost/cuttlefish-orchestration:latest
# image-builder.sh does not propagate podman build failures
sudo -u testrunner podman image exists localhost/cuttlefish-orchestration:latest

# Allow kokoro group to the source dir:
sudo chmod -R g+w /tmpfs/src

# Install bazel
command -v bazel &> /dev/null || sudo "${TOOL_DIR}/buildutils/installbazel.sh"

# Run as different user without sudo privileges
sudo -u testrunner CREDENTIAL_SOURCE=gce "${TOOL_DIR}/testutils/runcvde2etests_v2.sh" \
  -c ${config} \
  -i ${runner_index} \
  -n ${runners_total}
