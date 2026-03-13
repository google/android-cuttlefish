#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"
#ionice -c 3 -p "$SELFPID"

# This test creates 5 disks. Each of them are 2G. The 2nd disk
# contains a PV already. Simulate something is installed previously.

tmpdisk0="uboot_qemu_disk0.img"
tmpdisk1="uboot_qemu_disk1.img"
tmpdisk2="uboot_qemu_disk2.img"
tmpdisk3="uboot_qemu_disk3.img"
tmpdisk4="uboot_qemu_disk4.img"

if [ ! -e "${tmpdisk0}" ]; then
    truncate -s 2G "${tmpdisk0}"
fi

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 2G "${tmpdisk1}"
fi

if [ ! -e "${tmpdisk2}" ]; then
    truncate -s 2G "${tmpdisk2}"
fi

if [ ! -e "${tmpdisk3}" ]; then
    truncate -s 2G "${tmpdisk3}"
fi

if [ ! -e "${tmpdisk4}" ]; then
    truncate -s 2G "${tmpdisk4}"
fi

# partition disk4
/usr/sbin/sgdisk \
 "-n:1:18M:+32M" "-t:1:ef00" "-c:1:ubuesp" \
 "-A:1:set:0" "${tmpdisk4}"

/usr/sbin/sgdisk \
 "-n:2:50M:+50M" "-t:2:8305" "-c:2:ububoot" \
 "-A:2:set:2" "${tmpdisk4}"

/usr/sbin/sgdisk \
 "-n:3:100M:0" "-t:3:8e00" "-c:3:ubulvm" \
 "-A:3:set:0" "${tmpdisk4}"

system_partition=1
system_partition_start=$(partx -g -o START -s -n "${system_partition}" "${tmpdisk4}" | xargs)
system_partition_end=$(partx -g -o END -s -n "${system_partition}" "${tmpdisk4}" | xargs)
system_partition_num_sectors=$((${system_partition_end} - ${system_partition_start} + 1))
system_partition_num_vfat_blocks=$((${system_partition_num_sectors} / 2))
/usr/sbin/mkfs.vfat -n SYSTEM -F 16 --offset=${system_partition_start} "${tmpdisk4}" ${system_partition_num_vfat_blocks}

boot_partition=2
boot_partition_start=$(partx -g -o START -s -n "${boot_partition}" "${tmpdisk4}" | xargs)
boot_partition_end=$(partx -g -o END -s -n "${boot_partition}" "${tmpdisk4}" | xargs)
boot_partition_num_sectors=$((${boot_partition_end} - ${boot_partition_start} + 1))
boot_partition_offset=$((${boot_partition_start} * 512))
boot_partition_size=$((${boot_partition_num_sectors} * 512))

/usr/sbin/mke2fs -E offset=${boot_partition_offset} ${tmpdisk4} 50m
/usr/sbin/e2fsck -fy "${tmpdisk4}"?offset=${boot_partition_offset} || true

guestfish <<_EOF_
add ${tmpdisk0}
add ${tmpdisk1}
add ${tmpdisk2}
add ${tmpdisk3}
add ${tmpdisk4}
run
list-devices
list-partitions
pvcreate /dev/sda
pvcreate /dev/sdb
pvcreate /dev/sdc
pvcreate /dev/sdd
pvcreate /dev/sde3
pvs-full
vgcreate ubu-vg "/dev/sde3"
vgs-full
lvcreate-free ubu-lv ubu-vg 100
lvs-full
vg-activate-all true
mke2fs /dev/ubu-vg/ubu-lv
e2fsck-f /dev/ubu-vg/ubu-lv
list-filesystems
vgcreate home-vg "/dev/sda /dev/sdb /dev/sdc /dev/sdd"
vgs-full
lvcreate-free home-lv home-vg 100
lvs-full
vg-activate-all true
mke2fs /dev/home-vg/home-lv
e2fsck-f /dev/home-vg/home-lv
list-filesystems
_EOF_
