#!/usr/bin/env bash

set -e -x

sudo apt update

# environment variable and options to force answer prompts
sudo DEBIAN_FRONTEND=noninteractive apt -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" upgrade -y

# realpath .kokoro/..
readonly REPO_DIR="$(realpath "$(dirname "$0")"/..)"
readonly TOOL_DIR="${REPO_DIR}/tools"
readonly CACHE_CONFIG_FILE="${REPO_DIR}/.config/cache-config.env"

if [ -f "$CACHE_CONFIG_FILE" ]; then
    source "$CACHE_CONFIG_FILE"
fi

"${TOOL_DIR}/buildutils/build_packages.sh" -r "${BAZEL_REMOTE_CACHE}" -c "${CACHE_VERSION}"
# Add test user to the kokoro group so it has access to the source dir
"${TOOL_DIR}/testutils/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro

# Allow kokoro group to the source dir:
sudo chmod -R g+w /tmpfs/src

config="cvd"
if [[ "${ANDROID_CUTTLEFISH_KOKORO_BUILD_SCRIPT_ARGS:-}" == *"-g"* ]]; then
  config="cvd_with_gpu"
fi

# Run as different user without sudo privileges
sudo -u testrunner CREDENTIAL_SOURCE=gce "${TOOL_DIR}/testutils/runcvde2etests_v2.sh" -c "${config}"
