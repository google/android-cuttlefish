#!/bin/bash

set -e -x

# realpath .kokoro/..
REPO_DIR="$(realpath "$(dirname "$0")"/..)"
TOOL_DIR="${REPO_DIR}/tools/testutils"

"${TOOL_DIR}/build_packages.sh"

# Add test user to the kokoro group so it has access to the source dir
"${TOOL_DIR}/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro

# Run as different user without sudo privileges
sudo -u testrunner "${TOOL_DIR}/runcvde2etests.sh" e2etests/cvd/...
