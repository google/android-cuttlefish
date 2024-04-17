#!/bin/bash
#
# Copyright (C) 2024 The Android Open Source Project
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

set -e

usage() {
  echo "usage: $0 -o /path/to/output_dir"
}

output_dir=

while getopts "ho:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    o)
      output_dir="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      usage
      exit 1
      ;;
  esac
done

gen_cert() {
  local cert_file="${output_dir}/cert.pem"
  local key_file="${output_dir}/key.pem"
  rm -rf "${cert_file}"
  rm -rf "${key_file}"
  mkdir -p "${output_dir}"

  openssl req \
    -newkey rsa:4096 \
    -x509 \
    -sha256 \
    -days 36000 \
    -nodes \
    -out "${cert_file}" \
    -keyout "${key_file}" \
    -subj "/C=US"
}

gen_cert

