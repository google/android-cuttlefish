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

USER_CONFIG="/etc/podcvd.users"

if [ -f "$USER_CONFIG" ]; then
    while IFS=: read -r user range; do
        # Ignore empty or malformed lines
        if [ -z "$user" ] || [ -z "$range" ]; then
            continue
        fi

        # Ignore comment lines starting with '#'
        if [[ "$user" == \#* ]]; then
            continue
        fi

        if ! id "$user" &>/dev/null; then
            echo "User '$user' not found on system. Skipping."
            continue
        fi

        echo "Restoring ACLs for user '$user'..."
        setfacl -m "u:$user:rw" /dev/kvm || true
        setfacl -m "u:$user:rw" /dev/vhost-net || true
        setfacl -m "u:$user:rw" /dev/vhost-vsock || true
    done < "$USER_CONFIG"
fi
