#!/usr/bin/env bash

# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -o errexit -o nounset -o pipefail

readonly CONFIGS_DIR="$(realpath "$(dirname "$0")")/runcvde2etests_configs"
readonly REPO_DIR="$(realpath "$(dirname "$0")/../..")"
readonly OUTPUT_DIR="$(pwd)"
readonly CREDENTIAL_SOURCE="${CREDENTIAL_SOURCE:-}"

readonly CONFIG_CVD="cvd"
readonly CONFIG_CVD_WITH_GPU="cvd_with_gpu"

function print_usage() {
  >&2 echo "usage: $0 [-c <config>] [-i <runner_index>] [-n <runners_total>]"
}

config="${CONFIG_CVD}"
runner_index="1"
runners_total="1"

while getopts ":c:i:n:" opt; do
  case "${opt}" in
    c)
      config="${OPTARG}"
      ;;
    i)
      runner_index="${OPTARG}"
      ;;
    n)
      runners_total="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      print_usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      print_usage
      exit 1
      ;;
  esac
done

case "${config}" in
  "${CONFIG_CVD}") ;;
  "${CONFIG_CVD_WITH_GPU}") true ;;
  *)
    echo "ERROR: invalid configuration name '${config}'" >&2
    print_usage
    exit 1
    ;;
esac

if [[ ${runner_index} -lt 1 ]]; then
  echo "runner_index must be greater than 0" >&2
  print_usage
  exit 1
fi

if [[ ${runner_index} -gt ${runners_total} ]]; then
  echo "runner_index must be less than or equal to runners_total" >&2
  print_usage
  exit 1
fi

echo "config: ${config}"
echo "runner_index: ${runner_index}"
echo "runners_total: ${runners_total}"

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

echo "${CONFIGS_DIR}"

readonly QUERY_FILE="${CONFIGS_DIR}/${config}.query"
readonly TESTS_FILE="${CONFIGS_DIR}/${config}.tests"

if [[ ! -f "${QUERY_FILE}" ]]; then
  echo "ERROR: query file ${QUERY_FILE} not found" >&2
  exit 1
fi

if [[ ! -f "${TESTS_FILE}" ]]; then
  echo "ERROR: tests file ${TESTS_FILE} not found" >&2
  exit 1
fi

readonly QUERY=$(cat "${QUERY_FILE}")
readonly EXPECTED_TESTS=$(cat "${TESTS_FILE}")

cd "${REPO_DIR}/e2etests"

all_tests=$(bazel query --noshow_progress "${QUERY}" | grep -e "^\/\/" | LC_ALL=C sort)
all_tests_count=$(echo "${all_tests}" | wc --lines)

if [[ "${EXPECTED_TESTS}" == "${all_tests}" ]]; then
  echo "all tests count: ${all_tests_count}"
  echo "all tests"
  echo "${all_tests}"
else
  echo "ERROR: Expected tests mismatch" >&2
  diff -u <(echo "${EXPECTED_TESTS}") <(echo "${all_tests}") >&2
fi

remainder=$(( all_tests_count % runners_total ))
prev_runners_count=$(( runner_index - 1 ))
prev_runners_count_with_plus_one=$(( prev_runners_count < remainder ? prev_runners_count : remainder ))
prev_runners_count_plain=$(( prev_runners_count - prev_runners_count_with_plus_one  ))
quotient=$(( all_tests_count / runners_total ))
start_index=$(( (prev_runners_count_with_plus_one * (quotient + 1)) + (prev_runners_count_plain * quotient) + 1 ))
runner_tests_count=$(( quotient ))
if [[ ${runner_index} -le ${remainder} ]]; then
  runner_tests_count=$((runner_tests_count + 1))
fi
echo "runner first test index: ${start_index}"
echo "runner tests count: ${runner_tests_count}"
runner_tests=$(echo "${all_tests}" | sed -n "${start_index},+$((runner_tests_count-1)) p")
if [ -z "${runner_tests}" ]; then
  >&2 echo "runner has an empty list of tests to run"
  exit 1
fi
echo "runner tests"
echo "${runner_tests}"

# Gather test results regardless of status, but still return the exit code from
# those tests
trap gather_test_results EXIT

credential_arg=""
if [[ -n "$CREDENTIAL_SOURCE" ]]; then
  credential_arg="--test_env=CREDENTIAL_SOURCE=${CREDENTIAL_SOURCE}"
fi

# --zip_undeclared_test_outputs triggers the creation of the outputs.zip file
# everything written to $TEST_UNDECLARED_OUTPUTS_DIR is put into this zip
bazel test \
  ${credential_arg} \
  --zip_undeclared_test_outputs \
  ${runner_tests}

