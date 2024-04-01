#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"
#ionice -c 3 -p "$SELFPID"

# This test creates 5 disks. Each of them are 8G. The 2nd disk
# contains a PV already. Simulate something is installed previously.

tmpdisk0="uboot_qemu_disk0.img"
tmpdisk1="uboot_qemu_disk1.img"
tmpdisk2="uboot_qemu_disk2.img"
tmpdisk3="uboot_qemu_disk3.img"
tmpdisk4="uboot_qemu_disk4.img"

if [ ! -e "${tmpdisk0}" ]; then
    truncate -s 8G "${tmpdisk0}"
fi

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 8G "${tmpdisk1}"
fi

if [ ! -e "${tmpdisk2}" ]; then
    truncate -s 8G "${tmpdisk2}"
fi

if [ ! -e "${tmpdisk3}" ]; then
    truncate -s 8G "${tmpdisk3}"
fi

if [ ! -e "${tmpdisk4}" ]; then
    truncate -s 8G "${tmpdisk4}"
fi

# partition disk4
/sbin/sgdisk \
 "-n:1:18M:+32M" "-t:1:ef00" "-c:1:ubuesp" \
 "-A:1:set:0" "${tmpdisk4}"

/sbin/sgdisk \
 "-n:2:50M:+50M" "-t:2:8305" "-c:2:ububoot" \
 "-A:2:set:2" "${tmpdisk4}"

/sbin/sgdisk \
 "-n:3:100M:0" "-t:3:8e00" "-c:3:ubulvm" \
 "-A:3:set:0" "${tmpdisk4}"

system_partition=1
system_partition_start=$(partx -g -o START -s -n "${system_partition}" "${tmpdisk4}" | xargs)
system_partition_end=$(partx -g -o END -s -n "${system_partition}" "${tmpdisk4}" | xargs)
system_partition_num_sectors=$((${system_partition_end} - ${system_partition_start} + 1))
system_partition_num_vfat_blocks=$((${system_partition_num_sectors} / 2))
/sbin/mkfs.vfat -n SYSTEM -F 16 --offset=${system_partition_start} "${tmpdisk4}" ${system_partition_num_vfat_blocks}

boot_partition=2
boot_partition_start=$(partx -g -o START -s -n "${boot_partition}" "${tmpdisk4}" | xargs)
boot_partition_end=$(partx -g -o END -s -n "${boot_partition}" "${tmpdisk4}" | xargs)
boot_partition_num_sectors=$((${boot_partition_end} - ${boot_partition_start} + 1))
boot_partition_offset=$((${boot_partition_start} * 512))
boot_partition_size=$((${boot_partition_num_sectors} * 512))

/sbin/mke2fs -E offset=${boot_partition_offset} ${tmpdisk4} 50m
/sbin/e2fsck -fy "${tmpdisk4}"?offset=${boot_partition_offset} || true

sudo true

LOOPDEV0="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV0}" "${tmpdisk0}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV0}"
sleep 10

LOOPDEV1="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV1}" "${tmpdisk1}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV1}"
sleep 10

LOOPDEV2="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV2}" "${tmpdisk2}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV2}"
sleep 10

LOOPDEV3="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV3}" "${tmpdisk3}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV3}"
sleep 10

LOOPDEV4="$(sudo /sbin/losetup -f)"
sudo losetup -P "${LOOPDEV4}" "${tmpdisk4}"
sleep 10
sudo pvcreate --verbose -y -Zy "${LOOPDEV4}"p3
sleep 10
sudo vgcreate --verbose -y -Zy ubu-vg "${LOOPDEV4}"p3
sleep 10
sudo lvcreate --verbose -Zy -l +100%FREE -n"ubu-lv" "ubu-vg" --devices "${LOOPDEV4}"p3
sleep 10
sudo vgchange -ay ubu-vg --devices "${LOOPDEV4}"p3
sleep 10
sudo vgmknodes
sleep 10
sudo lvdisplay --readonly --devices "${LOOPDEV4}"p3
rootfs_lv_path=$(sudo lvdisplay -C -o "lv_path" --readonly --devices "${LOOPDEV4}"p3 | tail -1 | xargs)
sudo lvchange -ay "${rootfs_lv_path}" --devices "${LOOPDEV4}"p3 --verbose
sleep 10
sudo mke2fs ${rootfs_lv_path}
sudo e2fsck -p -f ${rootfs_lv_path} || true
sudo lvchange -an "${rootfs_lv_path}" --devices "${LOOPDEV4}"p3 --verbose
sleep 10

sudo vgcreate --verbose -y -Zy home-vg "${LOOPDEV0}" "${LOOPDEV1}" "${LOOPDEV2}" "${LOOPDEV3}"
sleep 10
sudo lvcreate --verbose -Zy -l +100%FREE -n"home-lv" "home-vg"
sleep 10
sudo vgchange -ay home-vg
sleep 10
sudo vgmknodes
sleep 10
sudo lvdisplay --readonly --devices "${LOOPDEV0}" --devices "${LOOPDEV1}" --devices "${LOOPDEV2}" --devices "${LOOPDEV3}"
home_lv_path=$(sudo lvdisplay -C -o "lv_path" --readonly --devices "${LOOPDEV0}" --devices "${LOOPDEV1}" --devices "${LOOPDEV2}" --devices "${LOOPDEV3}" | tail -1 | xargs)
sudo lvchange -ay "${home_lv_path}" --verbose
sleep 10
sudo mke2fs ${home_lv_path}
sudo e2fsck -p -f ${home_lv_path} || true
sudo lvchange -an "${home_lv_path}" --verbose
sleep 10

sudo losetup -d "${LOOPDEV4}"
sleep 10
sudo losetup -d "${LOOPDEV3}"
sleep 10
sudo losetup -d "${LOOPDEV2}"
sleep 10
sudo losetup -d "${LOOPDEV1}"
sleep 10
sudo losetup -d "${LOOPDEV0}"
sleep 10
