#!/usr/bin/env bash
#
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

set -e

username="$USER"
: "${username:?username is empty}"

setup_device_availability() {
    # Execute both usermod and setfacl, to make podcvd working right after
    # executing this script and rebooting the machine.
    sudo usermod -aG kvm "$username"
    sudo setfacl -m "u:$username:rw" /dev/kvm
    sudo setfacl -m "u:$username:rw" /dev/vhost-net
    sudo setfacl -m "u:$username:rw" /dev/vhost-vsock
}

setup_rootless_podman() {
    if grep -q "^$username:" /etc/subuid /etc/subgid; then
        echo "Skip adding subuid/subgid. However, you may need to configure" \
             "it manually. Watch here for details." \
             "https://docs.podman.io/en/latest/markdown/podman.1.html#rootless-mode"
    else
        # Find available subuid/subgid range and add them under username.
        local start_id=$(awk -F: '{print $2 + $3}' /etc/subuid /etc/subgid 2>/dev/null | sort -n | tail -1)
        start_id="${start_id:-100000}"
        local id_range="$start_id-$((start_id + 65535))"
        sudo usermod --add-subuids "$id_range" --add-subgids "$id_range" "$username"
    fi
    podman system migrate
    systemctl --user enable --now podman.socket
}

setup_device_availability
setup_rootless_podman
