#!/bin/bash
trap 'rm -f /tmp/ivshmem_socket /tmp/ivshmem_socket_client' INT KILL EXIT

ABS_DIR=$(dirname $(realpath $0))
WS=$(bazel info workspace)
BIN=$(bazel info bazel-bin)
BAZEL_DIR=${ABS_DIR##${WS}}
bazel build /${BAZEL_DIR}:launcher

which virsh &> /dev/null
if (( $? != 0 )); then
    echo "Please install libvirt-bin package."
    exit
fi

ip link show abr0 &>/dev/null
if (( $? != 0 )); then
    virsh net-create ${ABS_DIR}/network-abr0.xml
fi

${BIN}/${BAZEL_DIR}/launcher "$@"
