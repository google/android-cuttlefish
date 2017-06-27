#!/bin/bash
trap 'rm -f /tmp/ivshmem_socket /tmp/ivshmem_socket_client' INT KILL EXIT

DIR=$(dirname $(realpath $0))
WS=$(bazel info workspace)
BIN=$(bazel info bazel-bin)
DIR=${DIR##${WS}}
bazel build /${DIR}:launcher
${BIN}/${DIR}/launcher "$@"
