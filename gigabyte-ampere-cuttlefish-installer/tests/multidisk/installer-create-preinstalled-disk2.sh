#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"
#ionice -c 3 -p "$SELFPID"

# This test creates 2 disks. Each of them are 5G. The 2nd disk
# contains a PV already. Simulate something is installed previously.

tmpdisk1="uboot_qemu_disk1.img"
tmpdisk2="uboot_qemu_disk2.img"

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 5G "${tmpdisk1}"
fi

if [ ! -e "${tmpdisk2}" ]; then
    truncate -s 5G "${tmpdisk2}"
fi

# partition disk1
/usr/sbin/sgdisk \
 "-n:1:5M:0" "-t:1:8e00" "-c:1:ubu2lvm" \
 "-A:1:set:0" "${tmpdisk1}"

# partition disk2
/usr/sbin/sgdisk \
 "-n:1:18M:+32M" "-t:1:ef00" "-c:1:ubuesp" \
 "-A:1:set:0" "${tmpdisk2}"

/usr/sbin/sgdisk \
 "-n:2:50M:+50M" "-t:2:8305" "-c:2:ububoot" \
 "-A:2:set:2" "${tmpdisk2}"

/usr/sbin/sgdisk \
 "-n:3:100M:0" "-t:3:8e00" "-c:3:ubulvm" \
 "-A:3:set:0" "${tmpdisk2}"

system_partition=1
system_partition_start=$(partx -g -o START -s -n "${system_partition}" "${tmpdisk2}" | xargs)
system_partition_end=$(partx -g -o END -s -n "${system_partition}" "${tmpdisk2}" | xargs)
system_partition_num_sectors=$((${system_partition_end} - ${system_partition_start} + 1))
system_partition_num_vfat_blocks=$((${system_partition_num_sectors} / 2))
/usr/sbin/mkfs.vfat -n SYSTEM -F 16 --offset=${system_partition_start} "${tmpdisk2}" ${system_partition_num_vfat_blocks}

boot_partition=2
boot_partition_start=$(partx -g -o START -s -n "${boot_partition}" "${tmpdisk2}" | xargs)
boot_partition_end=$(partx -g -o END -s -n "${boot_partition}" "${tmpdisk2}" | xargs)
boot_partition_num_sectors=$((${boot_partition_end} - ${boot_partition_start} + 1))
boot_partition_offset=$((${boot_partition_start} * 512))
boot_partition_size=$((${boot_partition_num_sectors} * 512))

/usr/sbin/mke2fs -E offset=${boot_partition_offset} ${tmpdisk2} 50m
/usr/sbin/e2fsck -fy "${tmpdisk2}"?offset=${boot_partition_offset} || true

guestfish <<_EOF_
add ${tmpdisk1}
add ${tmpdisk2}
run
list-devices
list-partitions
pvcreate /dev/sda1
pvcreate /dev/sdb3
pvs-full
vgcreate ubu-vg "/dev/sdb3 /dev/sda1"
vgs-full
lvcreate-free ubu-lv ubu-vg 100
lvs-full
vg-activate-all true
list-filesystems
mke2fs /dev/ubu-vg/ubu-lv
e2fsck-f /dev/ubu-vg/ubu-lv
list-filesystems
_EOF_
