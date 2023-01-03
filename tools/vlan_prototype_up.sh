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

# Create the host networks for the VLAN prototype.
# Runs as root.
# Use at your own risk.

create_interface() {
	bridge="$(printf cvd-v${1}br-%02d $2)"
	tap="$(printf cvd-${1}vlan-%02d $2)"
	gateway="${3}.$((4*$2 - 3))"
	network="${3}.$((4*$2 - 4))/30"
	netmask="255.255.255.252"
	dhcp_range="${3}.$((4*$2 - 2)),${3}.$((4*$2 - 2))"

	/sbin/brctl addbr "${bridge}"
	/sbin/brctl stp "${bridge}" off
	/sbin/brctl setfd "${bridge}" 0
	/sbin/ifconfig "${bridge}" "${gateway}" netmask "${netmask}" up

	iptables -t nat -A POSTROUTING -s "${network}" -j MASQUERADE

	dnsmasq \
	--strict-order \
	--except-interface=lo \
	--interface="${bridge}" \
	--listen-address="${gateway}" \
	--bind-interfaces \
	--dhcp-range="${dhcp_range}" \
	--conf-file="" \
	--pid-file=/var/run/cuttlefish-dnsmasq-"${bridge}".pid \
	--dhcp-leasefile=/var/run/cuttlefish-dnsmasq-"${bridge}".leases \
	--dhcp-no-override

	ip link add link cvd-net-01 name "${tap}" type vlan id ${4}
	/sbin/ifconfig "${tap}" 0.0.0.0 up
	/sbin/brctl addif "${bridge}" "${tap}"
}

ip tuntap add dev cvd-net-01 mode tap group cvdnetwork
ifconfig cvd-net-01 0.0.0.0 up

create_interface w 1 192.168.93 11
create_interface m 1 192.168.94 12
create_interface i 1 192.168.95 13
