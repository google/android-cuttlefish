#!/bin/bash

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
