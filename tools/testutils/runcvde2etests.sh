#!/usr/bin/env bash

set -x -e

REPO_DIR="$(realpath "$(dirname "$0")/../..")"
OUTPUT_DIR="$(pwd)"

function gather_test_results() {
  # Don't immediately exit on error anymore
  set +e
  for d in "${REPO_DIR}"/e2etests/bazel-testlogs/cvd/*; do
    dir="${OUTPUT_DIR}/$(basename "$d")"
    mkdir -p "${dir}"
    cp "${d}/test.log" "${dir}/sponge_log.log"
    cp "${d}/test.xml" "${dir}/sponge_log.xml"
    if [[ -f "${d}/test.outputs/outputs.zip" ]]; then
      unzip "${d}/test.outputs/outputs.zip" -d "${dir}/device_logs"
    fi
    # Make sure everyone has access to the output files
    chmod -R a+rw "${dir}"
  done
}

cd "${REPO_DIR}/e2etests"

# Gather test results regardless of status, but still return the exit code from
# those tests
trap gather_test_results EXIT

# --zip_undeclared_test_outputs triggers the creation of the outputs.zip file
# everything written to $TEST_UNDECLARED_OUTPUTS_DIR is put into this zip
bazel test --zip_undeclared_test_outputs cvd/...
