#!/bin/bash

set -x -e

REPO_DIR="$(realpath "$(dirname "$0")/..")"
OUTPUT_DIR="$(pwd)"

function gather_test_results() {
  # Don't immediately exit on error anymore
  set +e
  for d in ./bazel-testlogs/*; do
    dir="${OUTPUT_DIR}/$(basename "$d")"
    mkdir -p "${dir}"
    cp "${d}/test.log" "${dir}/sponge_log.log"
    cp "${d}/test.xml" "${dir}/sponge_log.xml"
    if [[ -f "${d}/test.outputs/outputs.zip" ]]; then
      mkdir -p "${dir}"
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
bazel test "$@"

