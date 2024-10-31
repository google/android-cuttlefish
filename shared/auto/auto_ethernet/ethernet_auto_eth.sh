#!/system/bin/sh
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

# Set IP address for eth1 that corresponds to virtual ethernet in Cuttlefish.
# Default to 192.168.98.3
IP=$(getprop ro.boot.auto_eth_guest_addr "192.168.98.3")

echo Setting IP address for eth1: $IP > /dev/kmsg

ifconfig eth1 "$IP"
ip route add 192.168.98.0/24 dev eth1

## This allow loopback support
ip link set dev lo up