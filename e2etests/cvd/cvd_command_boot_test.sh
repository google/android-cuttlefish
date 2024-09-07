#!/bin/bash

set -e -x

BRANCH=""
TARGET=""
CREDENTIAL_SOURCE=""

while getopts "c:b:t:" opt; do
  case "${opt}" in
    b)
      BRANCH="${OPTARG}"
      ;;
    c)
      CREDENTIAL_SOURCE="${OPTARG}"
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
shift $((OPTIND-1))

if [[ "${BRANCH}" == "" ]]; then
  echo "Missing required -b argument"
fi

if [[ "${TARGET}" == "" ]]; then
  echo "Missing required -t argument"
fi

workdir="$(mktemp -d -t cvd_command_test.XXXXXX)"

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
  # Be nice, don't leave devices behind.
  cvd reset -y
}

# Regardless of whether and when a failure occurs logs must collected
trap collect_logs_and_cleanup EXIT

# Make sure to run in a clean environment, without any devices running
cvd reset -y

cvd fetch \
  --default_build="${BRANCH}/${TARGET}" \
  --target_directory="${workdir}" \
  --credential_source="${CREDENTIAL_SOURCE}"

(
  cd "${workdir}"
  cvd create --report_anonymous_usage_stats=y --undefok=report_anonymous_usage_stats
  cvd "$@"
  cvd rm
)
