#!/bin/bash

set -e -x

BRANCH=""
TARGET=""

while getopts "b:t:" opt; do
  case "${opt}" in
    b)
      BRANCH="${OPTARG}"
      ;;
    t)
      TARGET="${OPTARG}"
      ;;
    *)
      echo "Unknown flag: -${opt}" >&2
      echo "Usage: $0 -b BRANCH -t TARGET"
      exit 1
  esac
done

if [[ "${BRANCH}" == "" ]]; then
  echo "Missing required -b argument"
fi

if [[ "${TARGET}" == "" ]]; then
  echo "Missing required -t argument"
fi

workdir="$(mktemp -d -t cvd_boot_test.XXX)"

function collect_logs_and_cleanup() {
  # Don't immediately exit on failure anymore
  set +e
  if [[ -n "${TEST_UNDECLARED_OUTPUTS_DIR}" ]] && [[ -d "${TEST_UNDECLARED_OUTPUTS_DIR}" ]]; then
    cp "${workdir}"/*.log "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${workdir}"/cuttlefish_runtime/*.log "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${workdir}"/cuttlefish_runtime/logcat "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${workdir}"/cuttlefish_runtime/cuttlefish_config.json "${TEST_UNDECLARED_OUTPUTS_DIR}"
  fi
  rm -rf "${workdir}"
  # Be nice, dont' leave a server or devices behind.
  cvd reset -y
}

# Regardless of whether and when a failure occurs logs must collected
trap collect_logs_and_cleanup EXIT

# Make sure the server isn't running. Bazel tries to sandbox tests, but the cvd
# client can still connect to the server outside the sandbox and cause issues.
cvd reset -y

cvd fetch --default_build="${BRANCH}/${TARGET}" --target_directory="${workdir}"

(
  cd "${workdir}"
  HOME=$(pwd) bin/launch_cvd --daemon --report_anonymous_usage_stats=y --enable_sandbox=false
  HOME=$(pwd) bin/stop_cvd
)
