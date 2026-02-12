#!/usr/bin/env bash

echo ""
echo ""
echo ""
echo "NOTE: consider using Bazel's '--test_timeout=<large value>' when running"
echo "a XTS test for the first time as the XTS download may be slow!"
echo ""
echo "'cvd fetch' caches the downloads and subsequent runs should be faster."
echo ""
echo ""
echo ""

# Exit on failure:
set -e
# Print commands before running:
set -x

# Parse command line flags:
CREDENTIAL_SOURCE="${CREDENTIAL_SOURCE:-}"
SUBSTITUTIONS=""
CUTTLEFISH_CREATE_ARGS=""
CUTTLEFISH_FETCH_BRANCH=""
CUTTLEFISH_FETCH_TARGET=""
XML_CONVERTER_PATH=""
XTS_ARGS=""
XTS_FETCH_BRANCH=""
XTS_FETCH_TARGET=""
XTS_TYPE=""
for arg in "$@"; do
  case "${arg}" in
    --cuttlefish-create-args=*)
      CUTTLEFISH_CREATE_ARGS="${arg#*=}"
      ;;
    --cuttlefish-fetch-branch=*)
      CUTTLEFISH_FETCH_BRANCH="${arg#*=}"
      ;;
    --cuttlefish-fetch-target=*)
      CUTTLEFISH_FETCH_TARGET="${arg#*=}"
      ;;
    --credential-source=*)
      CREDENTIAL_SOURCE="${arg#*=}"
      ;;
    --xml-test-result-converter-path=*)
      XML_CONVERTER_PATH="${arg#*=}"
      ;;
    --xts-args=*)
      XTS_ARGS="${arg#*=}"
      ;;
    --xts-fetch-branch=*)
      XTS_FETCH_BRANCH="${arg#*=}"
      ;;
    --xts-fetch-target=*)
      XTS_FETCH_TARGET="${arg#*=}"
      ;;
    --xts-type=*)
      XTS_TYPE="${arg#*=}"
      ;;
    *)
      echo "Unknown flag: ${arg}" >&2
      exit 1
      ;;
  esac
done
if [ -z "${CUTTLEFISH_FETCH_BRANCH}" ]; then
  echo "Missing required --cuttlefish-fetch-branch flag."
  exit 1
fi
if [ -z "${CUTTLEFISH_FETCH_TARGET}" ]; then
  echo "Missing required --cuttlefish-fetch-targt flag."
  exit 1
fi
if [ -z "${XML_CONVERTER_PATH}" ]; then
  echo "Missing required --xml-test-result-converter-path flag."
  exit 1
fi
if [ -z "${XTS_ARGS}" ]; then
  echo "Missing required --xts-args flag."
  exit 1
fi
if [ -z "${XTS_FETCH_BRANCH}" ]; then
  echo "Missing required --xts-fetch-branch flag."
  exit 1
fi
if [ -z "${XTS_FETCH_TARGET}" ]; then
  echo "Missing required --xts-fetch-target flag."
  exit 1
fi
if [ -z "${XTS_TYPE}" ]; then
  echo "Missing required --xts-type flag."
  exit 1
fi
XML_CONVERTER_PATH="$(realpath ${XML_CONVERTER_PATH})"
echo "Parsed command line args:"
echo "  * CREDENTIAL_SOURCE: ${CREDENTIAL_SOURCE}"
echo "  * CUTTLEFISH_CREATE_ARGS: ${CUTTLEFISH_CREATE_ARGS}"
echo "  * CUTTLEFISH_FETCH_BRANCH: ${CUTTLEFISH_FETCH_BRANCH}"
echo "  * CUTTLEFISH_FETCH_TARGET: ${CUTTLEFISH_FETCH_TARGET}"
echo "  * XML_CONVERTER_PATH: ${XML_CONVERTER_PATH}"
echo "  * XTS_ARGS: ${XTS_ARGS}"
echo "  * XTS_FETCH_BRANCH: ${XTS_FETCH_BRANCH}"
echo "  * XTS_FETCH_TARGET: ${XTS_FETCH_TARGET}"
echo "  * XTS_TYPE: ${XTS_TYPE}"


RUN_DIRECTORY="$(pwd)"

workdir="$(mktemp -d -t cvd_xts_test.XXXXXX)"
cd "${workdir}"

XTS_DIRECTORY="${workdir}/test_suites"
XTS_LATEST_RESULTS_DIRECTORY=""
XTS_RUNNER=""
if [ "${XTS_TYPE}" == "cts" ]; then
  XTS_LATEST_RESULTS_DIRECTORY="${XTS_DIRECTORY}/android-cts/results/latest"
  XTS_RUNNER="${XTS_DIRECTORY}/android-cts/tools/cts-tradefed"
elif [ "${XTS_TYPE}" == "vts" ]; then
  XTS_LATEST_RESULTS_DIRECTORY="${XTS_DIRECTORY}/android-vts/results/latest"
  XTS_RUNNER="${XTS_DIRECTORY}/android-vts/tools/vts-tradefed"
else
  echo "Unsupported XTS type: ${XTS_TYPE}. Failure."
  exit 1
fi
XTS_LATEST_RESULT_XML="${XTS_LATEST_RESULTS_DIRECTORY}/test_result.xml"
XTS_LATEST_RESULT_CONVERTED_XML="${XTS_LATEST_RESULTS_DIRECTORY}/test.xml"

# Additional files to keep track of and save to the final test results directory:
CVD_CREATE_LOG_FILE="${workdir}/cvd_create_logs.txt"
CVD_FETCH_LOG_FILE="${workdir}/cvd_fetch_logs.txt"
XTS_LOG_FILE="${workdir}/xts_logs.txt"

function collect_logs_and_cleanup() {
  # Don't immediately exit on failure anymore
  set +e
  if [[ -n "${TEST_UNDECLARED_OUTPUTS_DIR}" ]] && [[ -d "${TEST_UNDECLARED_OUTPUTS_DIR}" ]]; then
    echo "Copying logs to test output directory..."

    cp "${workdir}"/*.log "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${workdir}"/cuttlefish_runtime/*.log "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${workdir}"/cuttlefish_runtime/logcat "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${workdir}"/cuttlefish_runtime/cuttlefish_config.json "${TEST_UNDECLARED_OUTPUTS_DIR}"

    cp "${CVD_FETCH_LOG_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${CVD_CREATE_LOG_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${XTS_LOG_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
  fi

  if [[ -n "${XML_OUTPUT_FILE}" ]]; then
    echo "Copying converted XML results for Bazel consumption..."
    cp "${XTS_LATEST_RESULT_CONVERTED_XML}" "${XML_OUTPUT_FILE}"
    echo "Copied!"
  fi

  rm -rf "${workdir}"

  # Be nice, don't leave devices behind.
  cvd reset -y
}

# Regardless of whether and when a failure occurs logs must collected
trap collect_logs_and_cleanup EXIT

# Make sure to run in a clean environment, without any devices running
cvd reset -y


# Fetch Cuttlefish and XTS:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Fetching Cuttlefish and ${XTS_TYPE}..."

credential_arg=""
if [[ -n "$CREDENTIAL_SOURCE" ]]; then
  credential_arg="--credential_source=${CREDENTIAL_SOURCE}"
fi

cvd fetch \
  --default_build="${CUTTLEFISH_FETCH_BRANCH}/${CUTTLEFISH_FETCH_TARGET}" \
  --test_suites_build="${XTS_FETCH_BRANCH}/${XTS_FETCH_TARGET}" \
  --target_directory="${workdir}" \
  ${credential_arg} \
  2>&1 | tee "${CVD_FETCH_LOG_FILE}"

echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Fetch completed!"


# Android CTS includes some files with a `kernel` suffix which confuses the
# Cuttlefish launcher prior to
# https://github.com/google/android-cuttlefish/commit/881728ed85329afaeb16e3b849d60c7a32fedcb7.
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Modifying `fetcher_config.json` for `kernel` file suffix workaround ..."
sed -i 's|_kernel"|_kernel_zzz"|g' ${workdir}/fetcher_config.json


# Create a new Cuttlefish device:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Creating a Cuttlefish device with: ${CUTTLEFISH_CREATE_ARGS}"

# Note: `eval` used because `CUTTLEFISH_CREATE_ARGS` might have been
# escaped by Bazel.
eval \
HOME="$(pwd)" \
cvd create \
  --report_anonymous_usage_stats=y \
  --undefok=report_anonymous_usage_stats \
  "${CUTTLEFISH_CREATE_ARGS}" \
  2>&1 | tee "${CVD_CREATE_LOG_FILE}"

echo "Cuttlefish device created!"


# Wait for the new Cuttlefish device to appear in adb:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Waiting for the Cuttlefish device to connect to adb..."
timeout --kill-after=30s 29s adb wait-for-device
if [ $? -eq 0 ]; then
  echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
  echo "Cuttlefish device connected to adb."
  adb devices
else
  echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
  echo "Timeout waiting for Cuttlefish device to connect to adb!"
  exit 1
fi


# Run XTS. Note: `eval` used because `XTS_ARGS` might have been
# escaped by Bazel.
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Running ${XTS_TYPE}..."
cd "${XTS_DIRECTORY}"
HOME="$(pwd)" \
eval ${XTS_RUNNER} run commandAndExit "${XTS_TYPE}" \
  --log-level-display=INFO \
  "${XTS_ARGS}" \
  2>&1 | tee "${XTS_LOG_FILE}"
echo "Finished running ${XTS_TYPE}!"


# Convert results to Bazel friendly format:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Converting ${XTS_TYPE} test result output to Bazel XML format..."
python3 ${XML_CONVERTER_PATH} \
    --input_xml_file="${XTS_LATEST_RESULT_XML}" \
    --output_xml_file="${XTS_LATEST_RESULT_CONVERTED_XML}"
echo "Converted!"


# Determine exit code by looking at XTS results:
echo "$(date +'%Y-%m-%dT%H:%M:%S%z')"
echo "Checking if any ${XTS_TYPE} tests failed..."
failures=$(cat ${XTS_LATEST_RESULT_CONVERTED_XML} | grep "<testsuites" | grep -E -o "failures=\"[0-9]+\"")
if [ "${failures}" = "failures=\"0\"" ]; then
  echo "${XTS_TYPE} passed!"
  exit 0
else
  echo "${XTS_TYPE} had failures!"
  exit 1
fi
