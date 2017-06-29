#!/bin/bash

pushd "$(dirname "$0")" > /dev/null 2>&1
SCRIPT_DIR="$(pwd)"
cd ../../..
TOP_DIR="$(pwd)"
popd

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
    -F 192.168.99.10,192.168.99.10 \
    --dhcp-option-force 1,255.255.255.255 \
    --dhcp-option-force 121,192.168.99.1/32,0.0.0.0,0.0.0.0/0,192.168.99.1

  echo "Good. Your IP address range is 192.168.99.10 - 192.168.99.13"
  echo "Configuring IP rules..."

  iptables -t nat -A POSTROUTING -s 192.168.99.0/24 -j MASQUERADE

  # This is required for DNSMASQ to work. DNSMASQ does not fill in packet
  # checksum, and neither does kernel, unless explicitly asked.
  # DHCP client checks checksums to confirm packet sanity, and without this
  # line all DHCP responses would be considered invalid and ignored.
  iptables -A POSTROUTING -t mangle -p udp --dport bootpc -s 192.168.99.0/24 -j CHECKSUM --checksum-fill

  sysctl -w net.ipv4.ip_forward=1
}

function die {
  echo "$@"
  read
  exit
}

if [ "$#" -ge "0" ]; then
  IMAGE_DIR=${1}
else
  IMAGE_DIR="."
fi

if [ $# -gt 1 ]; then
  INSTANCE_NUMBER=${2}
else
  INSTANCE_NUMBER="1"
fi

[ "$EUID" -ne 0 ] && die "Must run as super-user."

DATA_IMG=${IMAGE_DIR}/data-${INSTANCE_NUMBER}.img

# TODO(ender): This is a little scary:
#  It clobbers /data every execution
#  It will fail if the prebuilt data is > 10 GB
rm -f ${DATA_IMG}
cp --reflink=auto ${IMAGE_DIR}/userdata.img ${DATA_IMG}
truncate -s 10G ${DATA_IMG}
e2fsck -fy ${DATA_IMG}
resize2fs ${DATA_IMG}

if [ ! -e ${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img ]; then
  truncate -s 2G ${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img
fi
mkfs.ext4 -F ${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img

rm /var/run/shm/ivshmem

"${TOP_DIR}/ivshmem-server/ivshmem_server.sh" \
  --layoutfile="${TOP_DIR}/ivshmem-server/vsoc_mem.json" \
  --image_dir="${IMAGE_DIR}" \
  --script_dir="${SCRIPT_DIR}" \
  --instance_number="${INSTANCE_NUMBER}"
