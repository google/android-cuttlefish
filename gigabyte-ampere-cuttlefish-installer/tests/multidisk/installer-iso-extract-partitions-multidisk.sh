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

diskmain=$(ls uboot_qemu_disk*.img | head -1)

NUMOFPARTITIONS=$(partx -g "${diskmain}" | wc -l)

if [ ${NUMOFPARTITIONS} -ne 3 ]; then
    echo "Number of partitions incorrect: ${NUMOFPARTITIONS}"
    partx "${diskmain}"
    exit 1
fi

for DISK1 in uboot_qemu_disk*.img; do
    if [ x"${DISK1}" = x"${diskmain}" ]; then
	continue
    fi
    NUMOFPARTITIONS1=$(partx -g "${DISK1}" | wc -l)
    if [ ${NUMOFPARTITIONS1} -ne 1 ]; then
	echo "Number of partitions incorrect for ${DISK1}: ${NUMOFPARTITIONS1}"
	partx "${DISK1}"
	exit 1
    fi
done

rootfs_partition_tempfile="rootfs.img"
lvm_partition_tempfile="lvm"$(echo "${diskmain}" | sed 's/uboot_qemu_disk\(.*\)\.img/\1/')".img"
lvm_partition="3"
boot_partition_tempfile="boot.img"
boot_partition="2"

extract_image "${diskmain}" "${boot_partition}" "${boot_partition_tempfile}"
extract_image "${diskmain}" "${lvm_partition}" "${lvm_partition_tempfile}"
/usr/sbin/e2fsck -p -f "${boot_partition_tempfile}" || true

for DISK1 in uboot_qemu_disk*.img; do
    if [ x"${DISK1}" = x"${diskmain}" ]; then
	continue
    fi
    NUM1=$(echo "${DISK1}" | sed 's/uboot_qemu_disk\(.*\)\.img/\1/')
    extract_image "${DISK1}" 1 "lvm${NUM1}.img"
done

sudo true

LOOPDEVSPV="--ro"
for LVM1 in lvm*.img; do
    LOOPDEVSPV="${LOOPDEVSPV} -a ${LVM1}"
done

echo "Test pvdisplay on each PV. Should printout a warning."
for LVM1 in lvm*.img; do
    guestfish --ro -a "${LVM1}" run : pvs
done

echo "Test pvdisplay on all PV. Should be OK."
guestfish ${LOOPDEVSPV} run : pvs : vgs : lvs
LVSLIST=$(guestfish ${LOOPDEVSPV} run : lvs)
rootfs_lv_path=$(echo "${LVSLIST}" | tail -1)
#sudo dd if="${rootfs_lv_path}" of="${rootfs_partition_tempfile}" bs=512
touch guestfish_commands.txt
for LVM1 in lvm*.img; do
    echo "add-ro ${LVM1}" >> guestfish_commands.txt
done
cat >> guestfish_commands.txt <<_EOF_
run
pvs-full
vgs-full
lvs-full
list-filesystems
download ${rootfs_lv_path} ${rootfs_partition_tempfile}
_EOF_
guestfish < guestfish_commands.txt
sudo e2fsck -p -f "${rootfs_partition_tempfile}" || true
sleep 10
