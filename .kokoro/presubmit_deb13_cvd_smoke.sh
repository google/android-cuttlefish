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

set -o errexit -o nounset -o pipefail -o xtrace

REPO_DIR="$(realpath "$(dirname "$0")"/..)"
TOOL_DIR="${REPO_DIR}/tools"

"$(dirname "$0")/presubmit_cvd_e2etest_build.sh"

"${TOOL_DIR}/testutils/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro

sudo chmod -R g+w /tmpfs/src

command -v bazel &> /dev/null || sudo "${TOOL_DIR}/buildutils/installbazel.sh"

sudo -u testrunner CREDENTIAL_SOURCE=gce "${TOOL_DIR}/testutils/runcvde2etests_v2.sh" -c smoke
