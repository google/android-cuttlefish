#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"
#ionice -c 3 -p "$SELFPID"

# This test creates 2 disks. Each of them are 8G. The 2nd disk
# contains a PV already. Simulate something is installed previously.

tmpdisk1="uboot_qemu_disk1.img"
tmpdisk2="uboot_qemu_disk2.img"

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 8G "${tmpdisk1}"
fi

if [ ! -e "${tmpdisk2}" ]; then
    truncate -s 8G "${tmpdisk2}"
fi

# partition disk1
/sbin/sgdisk \
 "-n:1:5M:0" "-t:1:8e00" "-c:1:ubu2lvm" \
 "-A:1:set:0" "${tmpdisk1}"

# partition disk2
/sbin/sgdisk \
 "-n:1:18M:+32M" "-t:1:ef00" "-c:1:ubuesp" \
 "-A:1:set:0" "${tmpdisk2}"

/sbin/sgdisk \
 "-n:2:50M:+50M" "-t:2:8305" "-c:2:ububoot" \
 "-A:2:set:2" "${tmpdisk2}"

/sbin/sgdisk \
 "-n:3:100M:0" "-t:3:8e00" "-c:3:ubulvm" \
 "-A:3:set:0" "${tmpdisk2}"

system_partition=1
system_partition_start=$(partx -g -o START -s -n "${system_partition}" "${tmpdisk2}" | xargs)
system_partition_end=$(partx -g -o END -s -n "${system_partition}" "${tmpdisk2}" | xargs)
system_partition_num_sectors=$((${system_partition_end} - ${system_partition_start} + 1))
system_partition_num_vfat_blocks=$((${system_partition_num_sectors} / 2))
/sbin/mkfs.vfat -n SYSTEM -F 16 --offset=${system_partition_start} "${tmpdisk2}" ${system_partition_num_vfat_blocks}

boot_partition=2
boot_partition_start=$(partx -g -o START -s -n "${boot_partition}" "${tmpdisk2}" | xargs)
boot_partition_end=$(partx -g -o END -s -n "${boot_partition}" "${tmpdisk2}" | xargs)
boot_partition_num_sectors=$((${boot_partition_end} - ${boot_partition_start} + 1))
boot_partition_offset=$((${boot_partition_start} * 512))
boot_partition_size=$((${boot_partition_num_sectors} * 512))

/sbin/mke2fs -E offset=${boot_partition_offset} ${tmpdisk2} 50m
/sbin/e2fsck -fy "${tmpdisk2}"?offset=${boot_partition_offset} || true

sudo true

LOOPDEV2="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV2}" "${tmpdisk1}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV2}"p1
sleep 10

LOOPDEV="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV}" "${tmpdisk2}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV}"p3
sleep 10
sudo vgcreate --verbose -y -Zy ubu-vg "${LOOPDEV}"p3 "${LOOPDEV2}"p1
sleep 10
sudo lvcreate --verbose -Zy -l +100%FREE -n"ubu-lv" "ubu-vg" --devices "${LOOPDEV}"p3 --devices "${LOOPDEV2}"p1
sleep 10
sudo vgchange -ay ubu-vg --devices "${LOOPDEV}"p3 --devices "${LOOPDEV2}"p1
sleep 10
sudo vgmknodes
sleep 10
sudo lvdisplay --readonly --devices "${LOOPDEV}"p3 --devices "${LOOPDEV2}"p1
rootfs_lv_path=$(sudo lvdisplay -C -o "lv_path" --readonly --devices "${LOOPDEV}"p3 --devices "${LOOPDEV2}"p1 | tail -1 | xargs)
sudo lvchange -ay "${rootfs_lv_path}" --devices "${LOOPDEV}"p3 --devices "${LOOPDEV2}"p1 --verbose
sleep 10
sudo mke2fs ${rootfs_lv_path}
sudo e2fsck -p -f ${rootfs_lv_path} || true
sudo lvchange -an "${rootfs_lv_path}" --devices "${LOOPDEV}"p3 --devices "${LOOPDEV2}"p1 --verbose
sleep 10
sudo losetup -d "${LOOPDEV}"
sleep 10
sudo losetup -d "${LOOPDEV2}"
sleep 10
