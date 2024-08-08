#!/bin/bash

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
  echo "usage: $0 -b <BUILD_ID> -t <TARGET> -a <ARTIFACT_NAME> -o /path/to/artifact"
}

build_id=
build_target=
artifact_name=
output=

while getopts ":hb:t:a:o:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    b)
      build_id="${OPTARG}"
      ;;
    t)
      build_target="${OPTARG}"
      ;;
    a)
      artifact_name="${OPTARG}"
      ;;
    o)
      output="${OPTARG}"
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

getSignedUrlUrl="https://androidbuildinternal.googleapis.com/android/internal/build/v3/builds/${build_id}/${build_target}/attempts/latest/artifacts/${artifact_name}/url?redirect=false"
res=$(curl --fail "${getSignedUrlUrl}") 
signedUrl=$(echo "${res}" | jq -r '.signedUrl')
curl --fail ${signedUrl} -o ${output}

