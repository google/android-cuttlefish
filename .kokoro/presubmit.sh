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

if [[ "${ANDROID_CUTTLEFISH_KOKORO_BUILD_SCRIPT_ARGS:-}" == *"-p"* ]]; then
    retry sudo apt-get install -y docker.io
    sudo systemctl start docker || true

    sudo docker build -t android-cuttlefish-build:latest -f "${TOOL_DIR}/buildutils/cw/Containerfile" "${REPO_DIR}"
    sudo docker run --rm --network=host -v="${REPO_DIR}":/mnt/build -w /mnt/build android-cuttlefish-build:latest base -r "${BAZEL_REMOTE_CACHE}" -c "${CACHE_VERSION}"
    sudo docker run --rm --network=host -v="${REPO_DIR}":/mnt/build -w /mnt/build android-cuttlefish-build:latest container
    sudo docker run --rm --network=host -v="${REPO_DIR}":/mnt/build -w /mnt/build android-cuttlefish-build:latest frontend
    # Clean up temporary packaging directories created during debuild
    sudo rm -rf "${REPO_DIR}"/*/debian/tmp

    "${TOOL_DIR}/testutils/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro -p
    sudo -u testrunner "${REPO_DIR}/container/image/image-builder.sh" -c podman -m dev -t localhost/cuttlefish-orchestration:latest
else
    "${TOOL_DIR}/buildutils/build_packages.sh" -r "${BAZEL_REMOTE_CACHE}" -c "${CACHE_VERSION}"

    # Add test user to the kokoro group so it has access to the source dir
    "${TOOL_DIR}/testutils/prepare_host.sh" -d "${REPO_DIR}" -u testrunner -g kokoro
fi

# Allow kokoro group to the source dir:
sudo chmod -R g+w /tmpfs/src

# Run as different user without sudo privileges
sudo -u testrunner CREDENTIAL_SOURCE=gce "${TOOL_DIR}/testutils/runcvde2etests.sh" \
    ${ANDROID_CUTTLEFISH_KOKORO_BUILD_SCRIPT_ARGS}
