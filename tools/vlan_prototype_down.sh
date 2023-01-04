#!/bin/bash

# Copyright 2018 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Delete the host networks for the VLAN prototype.
# Runs as root.
# Use at your own risk.

delete_interface() {
	bridge="$(printf cvd-v${1}br-%02d $2)"
	tap="$(printf cvd-${1}vlan-%02d $2)"
	network="${3}.$((4*$2 - 4))/30"

	/sbin/ifconfig "${tap}" down
	ip link delete "${tap}"

	if [ -f /var/run/cuttlefish-dnsmasq-"${bridge}".pid ]; then
		kill $(cat /var/run/cuttlefish-dnsmasq-"${bridge}".pid)
	fi

	iptables -t nat -D POSTROUTING -s "${network}" -j MASQUERADE

	/sbin/ifconfig "${bridge}" down
	/sbin/brctl delbr "${bridge}"
}

delete_interface w 1 192.168.93
delete_interface m 1 192.168.94
delete_interface i 1 192.168.95

ip link delete cvd-net-01
