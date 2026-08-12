#!/usr/bin/env bash

set -x -e

REPO_DIR="$(realpath "$(dirname "$0")/../..")"
OUTPUT_DIR="$(pwd)"
CREDENTIAL_SOURCE="${CREDENTIAL_SOURCE:-}"

gpu_enabled=false
podcvd_enabled=false
while getopts "gp" opt; do
  case "${opt}" in
    g)
      gpu_enabled=true
      ;;
    p)
      podcvd_enabled=true
      ;;
    *)
    echo "Invalid option: -${opt}"
    echo "Usage: $0 [-g] [-p]"
    echo ""
    echo "Options"
    echo " -g  only run tests with the 'requires_gpu' tag"
    echo " -p  test podcvd instead of cvd"
    exit 1
    ;;
  esac
done

function gather_test_results() {
  # Don't immediately exit on error anymore
  set +e

  # Keep in sync with `.kokoro/presubmit*.cfg`:
  output_tests_directory="${OUTPUT_DIR}/kokoro_test_results"

  tests_directory="${REPO_DIR}/e2etests/bazel-testlogs/cvd"
  for file in $(find ${tests_directory} -name test.xml); do
    test_directory="$(dirname ${file})"
    test_directory_relative=${test_directory/#$tests_directory}
    outdir="${output_tests_directory}/${test_directory_relative}"
    mkdir -p "${outdir}"
    cp "${test_directory}/test.log" "${outdir}/sponge_log.log"
    cp "${test_directory}/test.xml" "${outdir}/sponge_log.xml"
    if [[ -f "${test_directory}/test.outputs/outputs.zip" ]]; then
      unzip "${test_directory}/test.outputs/outputs.zip" -d "${outdir}"
    fi
  done

  # Make sure everyone has access to the output files
  chmod -R a+rw "${output_tests_directory}"
}

cd "${REPO_DIR}/e2etests"

# Gather test results regardless of status, but still return the exit code from
# those tests
trap gather_test_results EXIT

test_tags="-requires_gpu"
if [[ "${gpu_enabled}" == true ]]; then
  test_tags="requires_gpu"
fi
podcvd_args=""
if [[ "${podcvd_enabled}" == true ]]; then
  test_tags+=",podcvd"
  podcvd_args+=" --test_env=CONTAINERS_STORAGE_CONF=/tmp/podcvd-podman-config/storage.conf"
  podcvd_args+=" --test_env=HOME=${HOME}"
  podcvd_args+=" --test_env=USE_PODCVD=true"
  podcvd_args+=" --test_env=XDG_RUNTIME_DIR=/run/user/$(id -u)"
fi
bazel_test_tag_filter_arg="--test_tag_filters=${test_tags}"

credential_arg=""
if [[ -n "$CREDENTIAL_SOURCE" ]]; then
    credential_arg="--test_env=CREDENTIAL_SOURCE=${CREDENTIAL_SOURCE}"
fi

# --zip_undeclared_test_outputs triggers the creation of the outputs.zip file
# everything written to $TEST_UNDECLARED_OUTPUTS_DIR is put into this zip
bazel test \
  ${bazel_test_tag_filter_arg} \
  ${credential_arg} \
  ${podcvd_args} \
  --zip_undeclared_test_outputs \
  cvd/...
