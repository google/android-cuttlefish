#!/bin/bash

function die {
  echo "$@"
  sleep 10
  exit
}

SCRIPT_DIR=$(dirname "$0")

if [ "$#" -ge "0" ]; then
  IMAGE_DIR=${1}
else
  IMAGE_DIR="."
fi

[ "$EUID" -ne 0 ] && die "Must run as super-user."
[ ! -e ${IMAGE_DIR}/kernel ] && die "Need kernel."
[ ! -e ${IMAGE_DIR}/gce_ramdisk.img ] && die "Need GCE ramdisk."
[ ! -e ${IMAGE_DIR}/system.img ] && die "Need system.img."
[ ! -e ${IMAGE_DIR}/userdata.img ] && die "Need userdata.img."

if [ ! -e ${IMAGE_DIR}/data.img ]; then
  truncate -s 2G ${IMAGE_DIR}/data.img
  mkfs.ext4 -F ${IMAGE_DIR}/data.img
fi

qemu-system-x86_64 \
  -smp 4 \
  -m 2048 \
  -enable-kvm \
  -nographic \
  -serial tcp::4000,server \
  -serial tcp::4001,server \
  -netdev type=tap,id=net0,ifname=android0,script=${SCRIPT_DIR}/android-ifup,downscript=${SCRIPT_DIR}/android-ifdown \
  -device e1000,netdev=net0 \
  -kernel ${IMAGE_DIR}/kernel \
  -initrd ${IMAGE_DIR}/gce_ramdisk.img \
  -hda ${IMAGE_DIR}/android_system_disk_syslinux.img \
  -drive file=${IMAGE_DIR}/ramdisk.img,index=0,if=virtio,media=disk \
  -drive file=${IMAGE_DIR}/system.img,index=1,if=virtio,media=disk \
  -drive file=${IMAGE_DIR}/userdata.img,index=2,if=virtio,media=disk \
  -drive file=${IMAGE_DIR}/data.img,index=3,if=virtio,media=disk \
  -append "console=ttyS0 androidboot.hardware=gce_x86 androidboot.console=ttyS0 security=selinux androidboot.selinux=permissive enforcing=0 loop.max_part=7 QEMU"
