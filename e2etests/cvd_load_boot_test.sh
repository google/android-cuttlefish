#!/bin/bash

set -e -x

if [[ $# != 1 ]]; then
  echo "Usage: $0 ENVIRONMENT_SPECIFICATION_FILE"
  exit 1
fi

ls

ENV_FILE="$1"
CMD_OUT="cvd_load_stdout.txt"
CMD_ERR="cvd_load_stderr.txt"

function collect_logs_and_cleanup() {
  # Don't immediately exit on failure anymore
  set +e

  if [[ -n "${TEST_UNDECLARED_OUTPUTS_DIR}" ]] && [[ -d "${TEST_UNDECLARED_OUTPUTS_DIR}" ]]; then
    cp "${ENV_FILE}" "${TEST_UNDECLARED_OUTPUTS_DIR}/environment.json"
    cp "${CMD_OUT}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    cp "${CMD_ERR}" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    # TODO(b/324650975): cvd doesn't print very useful information yet so file locations must be extracted this way
    home_dir="$(grep -o -E 'HOME="/tmp/cvd/[0-9a-zA-Z/_]+"' "${CMD_ERR}" | cut -d= -f2 | grep -o -E '[^"]*')"
    artifacts_dir="$(grep -o -E '\-\-target_directory=/tmp/cvd/[0-9a-zA-Z/_]+' "${CMD_ERR}" | cut -d= -f2 | grep -o -E '[^"]*')"

    cp "${artifacts_dir}/fetch.log" "${TEST_UNDECLARED_OUTPUTS_DIR}"
    for instance_dir in "${home_dir}"/cuttlefish/instances/*; do
      instance="$(basename "${instance_dir}")"
      for log_file in "${instance_dir}"/logs/*; do
        base_name="$(basename "${log_file}")"
        cp "${log_file}" "${TEST_UNDECLARED_OUTPUTS_DIR}/${instance}_${base_name}"
      done
      cp "${instance_dir}"/cuttlefish_config.json "${TEST_UNDECLARED_OUTPUTS_DIR}/${instance}_cuttlefish_config.json"
    done
  fi

  # Be nice, dont' leave a server or devices behind.
  cvd reset -y
}

trap collect_logs_and_cleanup EXIT

# Make sure there is no cvd server around
cvd reset -y

cvd load "${ENV_FILE}" >"${CMD_OUT}" 2>"${CMD_ERR}"
