#!/bin/bash

function die {
  echo "$@"
  read
  exit
}

SCRIPT_DIR=$(dirname "$0")

if [ "$#" -ge "0" ]; then
  IMAGE_DIR=${1}
else
  IMAGE_DIR="."
fi

if [ "$#" -ge "1" ]; then
  INSTANCE_NUMBER=${2}
else
  INSTANCE_NUMBER="1"
fi

# :A:V:D:1:2
MACADDR=$(printf '00:41:56:44:%02X:%02X\n' $(( INSTANCE_NUMBER / 10 )) $(( INSTANCE_NUMBER % 10 )))

[ "$EUID" -ne 0 ] && die "Must run as super-user."
[ ! -e ${IMAGE_DIR}/kernel ] && die "Need kernel."
[ ! -e ${IMAGE_DIR}/gce_ramdisk.img ] && die "Need GCE ramdisk."
[ ! -e ${IMAGE_DIR}/system.img ] && die "Need system.img."
[ ! -e ${IMAGE_DIR}/userdata.img ] && die "Need system.img."

DATA_IMG=${IMAGE_DIR}/data-${INSTANCE_NUMBER}.img

rm -f ${DATA_IMG}
cp --reflink=auto ${IMAGE_DIR}/userdata.img ${DATA_IMG}
truncate -s 10G ${DATA_IMG}
e2fsck -fy ${DATA_IMG}
resize2fs ${DATA_IMG}

if [ ! -e ${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img ]; then
  truncate -s 2G ${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img
fi
mkfs.ext4 -F ${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img


qemu-system-x86_64 \
  -smp 4 \
  -m 2048 \
  -enable-kvm \
  -nographic \
  -serial unix:var/avd/android-${INSTANCE_NUMBER}-kernel \
  -serial unix:var/avd/android-${INSTANCE_NUMBER}-logcat \
  -netdev type=tap,id=net0,ifname=android${INSTANCE_NUMBER},script=${SCRIPT_DIR}/android-ifup,downscript=${SCRIPT_DIR}/android-ifdown \
  -device e1000,netdev=net0,mac=${MACADDR} \
  -kernel ${IMAGE_DIR}/kernel \
  -initrd ${IMAGE_DIR}/gce_ramdisk.img \
  -drive file=${IMAGE_DIR}/ramdisk.img,index=0,if=virtio,media=disk \
  -drive file=${IMAGE_DIR}/system.img,index=1,if=virtio,media=disk \
  -drive file=${IMAGE_DIR}/data-${INSTANCE_NUMBER}.img,index=2,if=virtio,media=disk \
  -drive file=${IMAGE_DIR}/cache-${INSTANCE_NUMBER}.img,index=3,if=virtio,media=disk \
  -append "console=ttyS0 androidboot.hardware=vsoc androidboot.console=ttyS0 security=selinux androidboot.selinux=permissive enforcing=0 loop.max_part=7 AVD_NESTED_VM"
