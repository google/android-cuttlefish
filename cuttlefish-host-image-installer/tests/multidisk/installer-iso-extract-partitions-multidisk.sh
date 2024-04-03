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
/sbin/e2fsck -p -f "${boot_partition_tempfile}" || true

for DISK1 in uboot_qemu_disk*.img; do
    if [ x"${DISK1}" = x"${diskmain}" ]; then
	continue
    fi
    NUM1=$(echo "${DISK1}" | sed 's/uboot_qemu_disk\(.*\)\.img/\1/')
    extract_image "${DISK1}" 1 "lvm${NUM1}.img"
done

sudo true

LOOPDEVS=""
LOOPDEVSPV=""
for LVM1 in lvm*.img; do
    LOOPDEV="$(sudo /sbin/losetup -f)"
    LOOPDEVS="${LOOPDEVS} ${LOOPDEV}"
    LOOPDEVSPV="${LOOPDEVSPV} --devices ${LOOPDEV}"
    sudo losetup -r "${LOOPDEV}" "${LVM1}"
    sleep 10
done

echo "Test pvdisplay on each PV. Should printout a warning."
for LOOPDEV in ${LOOPDEVS}; do
    sudo pvdisplay --readonly --devices "${LOOPDEV}"
done

echo "Test pvdisplay on all PV. Should be OK."
sudo pvdisplay --readonly ${LOOPDEVSPV}
sudo vgdisplay --readonly ${LOOPDEVSPV}
sudo lvdisplay --readonly ${LOOPDEVSPV}
rootfs_lv_path=$(sudo lvdisplay -C -o "lv_path" --readonly ${LOOPDEVSPV} | tail -1 | xargs)
rootfs_dev_path=$(sudo lvdisplay -C -o "lv_dm_path" --readonly ${LOOPDEVSPV} | tail -1 | xargs)
sudo lvchange -ay "${rootfs_lv_path}" ${LOOPDEVSPV} --verbose
sleep 10
sudo dd if="${rootfs_lv_path}" of="${rootfs_partition_tempfile}" bs=512
sudo /sbin/e2fsck -p -f "${rootfs_partition_tempfile}" || true
sudo lvchange -an "${rootfs_lv_path}" ${LOOPDEVSPV} --verbose
sleep 10
for LOOPDEV in ${LOOPDEVS}; do
    sudo losetup -d "${LOOPDEV}"
    sleep 10
done
