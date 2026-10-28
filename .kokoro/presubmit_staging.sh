#!/usr/bin/env bash

set -o errexit -o nounset -o pipefail -o xtrace

# realpath .kokoro/..
readonly REPO_DIR="$(realpath "$(dirname "$0")"/..)"
readonly TOOL_DIR="${REPO_DIR}/tools"

# Fail if the VM image name is different than the expected one.
#
# IMPORTANT!!! Do not remove this check. Staging jobs exist to validate a new
# VM image, and there is a race condition where a job can still be scheduled
# on a VM created from the previous image. Without this check such a run would
# pass and the new image would be marked as verified without ever being used.
if [[ -z "${EXPECTED_VM_IMAGE_NAME:-}" ]]; then
    echo "EXPECTED_VM_IMAGE_NAME is not set" >&2
    exit 1
fi
readonly VM_IMAGE="$(curl -sf -H 'Metadata-Flavor: Google' \
    http://metadata.google.internal/computeMetadata/v1/instance/image || true)"
echo "Kokoro VM image: ${VM_IMAGE:-unknown}"
if [[ "${VM_IMAGE##*/}" != "${EXPECTED_VM_IMAGE_NAME}" ]]; then
    echo "VM image mismatch: expected ${EXPECTED_VM_IMAGE_NAME}, got ${VM_IMAGE##*/}" >&2
    exit 1
fi

if [[ -r /etc/os-release ]]; then
    source /etc/os-release
fi

if [[ "${VERSION_ID:-}" == "12" ]]; then
    CACHE_CONFIG_FILE="${REPO_DIR}/.config/cache-config.env"
elif [[ "${VERSION_ID:-}" == "13" ]]; then
    CACHE_CONFIG_FILE="${REPO_DIR}/.config/cache-config-debian-13.env"
else
    echo "Unsupported Debian version: ${VERSION_ID:-unknown}" >&2
    exit 1
fi
readonly CACHE_CONFIG_FILE

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
