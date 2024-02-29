#!/bin/bash

set -e -x

TOOL_DIR="$(realpath "$(dirname "$0")")"

# Add test user to the kokoro group so it has access to the source dir
"${TOOL_DIR}/prepare_host.sh" -u testrunner -g kokoro

# Run as different user without sudo privileges
sudo -u testrunner "${TOOL_DIR}/runtests.sh" ...
