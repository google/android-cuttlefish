#!/bin/bash

extract_image() {
    disk1e="$1"
    part1e="$2"
    out1e="$3"

    partition_start=$(partx -g -o START -s -n "${part1e}" "${disk1e}" | xargs)
    partition_end=$(partx -g -o END -s -n "${part1e}" "${disk1e}" | xargs)
    partition_num_sectors=$((${partition_end} - ${partition_start} + 1))
    partition_offset=$((${partition_start} * 512))
    partition_size=$((${partition_num_sectors} * 512))

    dd if="${disk1e}" of="${out1e}" bs=512 skip="${partition_start}" \
       count="${partition_num_sectors}"
}

disk="uboot_qemu_disk1.img"

NUMOFPARTITIONS=$(partx -g "${disk}" | wc -l)

if [ ${NUMOFPARTITIONS} -ne 3 ]; then
    echo "Number of partitions incorrect: ${NUMOFPARTITIONS}"
    partx "${disk}"
    exit 1
fi

rootfs_partition_tempfile="rootfs.img"
lvm_partition_tempfile="lvm.img"
lvm_partition="3"
boot_partition_tempfile="boot.img"
boot_partition="2"

extract_image "${disk}" "${boot_partition}" "${boot_partition_tempfile}"
extract_image "${disk}" "${lvm_partition}" "${lvm_partition_tempfile}"
/usr/sbin/e2fsck -p -f "${boot_partition_tempfile}" || true

LVSLIST=$(guestfish --ro -a "${lvm_partition_tempfile}" run : lvs)
rootfs_lv_path=$(echo "${LVSLIST}" | tail -1)
#sudo dd if="${rootfs_lv_path}" of="${rootfs_partition_tempfile}" bs=512
guestfish <<_EOF_
add-ro ${lvm_partition_tempfile}
run
pvs-full
vgs-full
lvs-full
list-filesystems
download ${rootfs_lv_path} ${rootfs_partition_tempfile}
_EOF_
sudo e2fsck -p -f "${rootfs_partition_tempfile}" || true
sleep 10
