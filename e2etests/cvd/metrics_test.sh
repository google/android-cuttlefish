#!/usr/bin/env bash

set -e -x

BRANCH=""
TARGET=""
CREDENTIAL_SOURCE="${CREDENTIAL_SOURCE:-}"

while getopts "b:c:t:" opt; do
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

credential_arg=""
if [[ -n "$CREDENTIAL_SOURCE" ]]; then
    credential_arg="--credential_source=${CREDENTIAL_SOURCE}"
fi

cvd fetch \
  --default_build="${BRANCH}/${TARGET}" \
  --target_directory="${workdir}" \
  ${credential_arg}

(
  cd "${workdir}"

  # generate instantiation, start, and boot complete events
  cvd create --host_path="${workdir}" --product_path="${workdir}"

  # verify transmission by detecting existence of the metrics directory and the debug event files
  metrics_dir=`cvd fleet 2> /dev/null | jq --raw-output '.groups[0].metrics_dir'`
  if ! [[ -d "${metrics_dir}" ]]; then
    echo "metrics directory not found"
    exit 1
  fi

  # file prefixes sourced from `cuttlefish/host/libs/metrics/event_type.cc::EventTypeString` function
  if ! [[ -f "`ls ${metrics_dir}/device_instantiation*.txtpb`" ]]; then
    echo "metrics not transmitted for the expected instantiation event"
    exit 1
  fi
  if ! [[ -f "`ls ${metrics_dir}/device_boot_start*.txtpb`" ]]; then
    echo "metrics not transmitted for the expected start event"
    exit 1
  fi
  if ! [[ -f "`ls ${metrics_dir}/device_boot_complete*.txtpb`"  ]]; then
    echo "metrics not transmitted for the expected boot complete event"
    exit 1
  fi

  # generate stop event and verify transmission
  cvd stop
  if ! [[ -f "`ls ${metrics_dir}/device_stop*.txtpb`" ]]; then
    echo "metrics not transmitted for the expected stop event"
    exit 1
  fi
)
