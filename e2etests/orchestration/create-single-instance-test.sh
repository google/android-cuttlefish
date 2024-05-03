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

set -ex

usage="usage: $0 -d build_id -t target"

build_id=
target=

while getopts ":hd:t:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    d)
      build_id="${OPTARG}"
      ;;
    t)
      target="${OPTARG}"
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

# Wait for the service to be available 
#
# $1 - http port to listen on
wait_for_service() {
  local counter=0
  local wait_secs=4
  while [[ "$counter" -lt 3 ]]
  do
    sleep ${wait_secs} 
    if curl -sS "http://0.0.0.0:${1}/_debug/statusz"; then
      return 
    else
      wait_secs=$((wait_secs*2))
    fi
    counter=$((counter+1))
  done

  echo "timeout starting service"
  exit 1
}

http_port="61$(($RANDOM%10))$(($RANDOM%10))$(($RANDOM%10))"

# $1 - container name
# $2 - image name 
function collect_logs_and_cleanup() {
  # Don't immediately exit on failure anymore
  set +e

  curl -sS -X POST http://0.0.0.0:${http_port}/runtimeartifacts/:pull \
    --output "${TEST_UNDECLARED_OUTPUTS_DIR}/host_bugreport.zip"

  docker rm --force ${1}
  docker rmi ${2}
}

# 
# Prepare environment  
#
load_output=$(docker load -i docker/orchestration-image.tar)
image_name=$(echo "${load_output}" | sed -nr 's/^Loaded image: (.*)$/\1/p')
container_id=$(docker run \
  --log-driver json-file --log-opt max-size=10m \
  --privileged -d \
  -p ${http_port}:2080 \
  -it ${image_name} \
)
trap 'collect_logs_and_cleanup ${container_id} ${image_name}' EXIT
wait_for_service ${http_port}
echo "http service listening on port: ${http_port}"

# 
# Execute Test
# 
expected_res='{"cvds":[{"group":"cvd","name":"1","build_source":{},"status":"Running","displays":["720 x 1280 ( 320 )"],"webrtc_device_id":"cvd-1","adb_serial":"0.0.0.0:6520"}]}'
payload='
{
    "cvd": {
        "build_source": {
          "android_ci_build_source": {
              "main_build": {
                  "build_id": "'${build_id}'",
                  "target": "'${target}'"
              }
          }
        }
    }
}'
res=$(echo $payload | \
  curl -sS -X POST http://0.0.0.0:${http_port}/cvds \
    -H "Expect:" \
    -H 'Content-Type: text/json; charset=utf-8' \
    -d @-\
)
op=$(echo "${res}" | jq -r '.name')

res=$(curl -sS -X POST http://0.0.0.0:${http_port}/operations/${op}/:wait)

if [ "$res" != "$expected_res" ];
then
  printf "invalid response\n expected: '${expected_res}'\n got: '${res}'"
  exit 1
fi
