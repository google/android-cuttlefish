#!/usr/bin/env bash

set -e -x

retry() {
  local attempt=1
  while true; do
    if "$@"; then
      break
    fi
    if ((attempt++ < 20)); then
      echo "Retrying after ${attempt} attempts:" "$@"
      sleep 30
    else
      echo "Failed to run command:" "$@"
      return 1
    fi
  done
}

retry sudo apt update

# environment variable and options to force answer prompts
retry sudo DEBIAN_FRONTEND=noninteractive apt -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confold" upgrade -y

# realpath .kokoro/..
REPO_DIR="$(realpath "$(dirname "$0")"/..)"
TOOL_DIR="${REPO_DIR}/tools"
CACHE_CONFIG_FILE="${REPO_DIR}/.config/cache-config.env"

if [ -f "$CACHE_CONFIG_FILE" ]; then
    source "$CACHE_CONFIG_FILE"
fi

"${TOOL_DIR}/buildutils/build_packages.sh" -r "${BAZEL_REMOTE_CACHE}" -c "${CACHE_VERSION}"
if [[ "${ANDROID_CUTTLEFISH_KOKORO_BUILD_SCRIPT_ARGS:-}" == *"-p"* ]]; then
    if [[ "${ANDROID_CUTTLEFISH_KOKORO_BUILD_SCRIPT_ARGS:-}" == *"-g"* ]]; then
        sudo install -m 0755 -d /etc/apt/keyrings
        sudo curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey -o /etc/apt/keyrings/nvidia-container-toolkit.asc
        sudo curl -sL https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list -o /etc/apt/sources.list.d/nvidia-container-toolkit.list
        sudo sed -i 's#deb https://#deb [signed-by=/etc/apt/keyrings/nvidia-container-toolkit.asc] https://#g' /etc/apt/sources.list.d/nvidia-container-toolkit.list
        retry sudo apt update
        retry sudo apt-get install -y nvidia-container-toolkit
    fi
    retry sudo apt-get install -y podman
    "${TOOL_DIR}/testutils/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro -p
    sudo -u testrunner "${REPO_DIR}/container/image/image-builder.sh" -c podman -m dev -t localhost/cuttlefish-orchestration:latest
else
    # Add test user to the kokoro group so it has access to the source dir
    "${TOOL_DIR}/testutils/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro
fi

# Allow kokoro group to the source dir:
sudo chmod -R g+w /tmpfs/src

# Run as different user without sudo privileges
sudo -u testrunner CREDENTIAL_SOURCE=gce "${TOOL_DIR}/testutils/runcvde2etests.sh" \
    ${ANDROID_CUTTLEFISH_KOKORO_BUILD_SCRIPT_ARGS}
