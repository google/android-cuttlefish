#!/bin/bash

SCRIPT_DIR=$(dirname "$0")

if [ "$EUID" -ne 0 ]; then
  echo "Please run with super-user privileges."
  exit
fi

trap ctrl_c INT EXIT

function ctrl_c() {
  echo "Killing dnsmasq..."
  kill $(</var/run/dnsmasq-android.pid)
  rm /var/run/dnsmasq-android.pid
  echo "Cleaning up..."
  iptables -D POSTROUTING -t mangle -p udp --dport bootpc -s 192.168.99.0/24 -j CHECKSUM --checksum-fill
  iptables -t nat -D PREROUTING --src 192.168.99.0/24 --dst 169.254.169.254 -p tcp --dport 80 -j REDIRECT --to-destination 192.168.99.1:16925
  iptables -t nat -D POSTROUTING -s 192.168.99.0/24 -j MASQUERADE
  sysctl -w net.ipv4.ip_forward=0

  ip link delete abr0
}

[ -e /var/run/dnsmasq/android.pid ] || {
  echo "Creating bridge..."
  ip link add abr0 type bridge
  ip link set dev abr0 up
  echo "Configuring bridge addresses..."
  ip addr add 192.168.99.1/24 dev abr0

  echo "Starting DNSMASQ"
  dnsmasq \
    -x /var/run/dnsmasq-android.pid \
    --interface=abr0 --except-interface=lo --bind-interfaces \
    -F 192.168.99.10,192.168.99.13 \
    --dhcp-option-force 1,255.255.255.255 \
    --dhcp-option-force 121,192.168.99.1/32,0.0.0.0,0.0.0.0/0,192.168.99.1

  echo "Good. Your IP address range is 192.168.99.10 - 192.168.99.13"
  echo "Configuring IP rules..."

  iptables -t nat -A POSTROUTING -s 192.168.99.0/24 -j MASQUERADE
  iptables -t nat -A PREROUTING --src 192.168.99.0/24 --dst 169.254.169.254 -p tcp --dport 80 -j DNAT --to-destination 192.168.99.1:16925

  # This is required for DNSMASQ to work. DNSMASQ does not fill in packet
  # checksum, and neither does kernel, unless explicitly asked.
  # DHCP client checks checksums to confirm packet sanity, and without this
  # line all DHCP responses would be considered invalid and ignored.
  iptables -A POSTROUTING -t mangle -p udp --dport bootpc -s 192.168.99.0/24 -j CHECKSUM --checksum-fill

  sysctl -w net.ipv4.ip_forward=1
}

python ${SCRIPT_DIR}/pseudo_metadataserver.py 192.168.99.1 16925 ${SCRIPT_DIR}/metadata.json
