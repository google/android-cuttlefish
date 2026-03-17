#!/bin/bash

# Emulate debian-installer's list-devices
list-devices() {
    case "$1" in
    disk)
        lsblk -pnlo NAME --filter "TYPE == 'disk'"
        ;;
    partition)
        lsblk -pnlo NAME --filter "TYPE == 'part'"
        ;;
    cd)
        lsblk -pnlo NAME --filter "TYPE == 'rom'"
        ;;
    *)
        echo "Usage: list-devices [disk|partition|cd]"
        return 1
        ;;
    esac
}

# Only run on tty1
MYTTY=$(tty)
echo "My tty is ${MYTTY}"
if [ x"${MYTTY}" != x"/dev/tty1" ]; then
    echo "TTY is not tty1"
    exit 0
fi

# Prevent re-entrance
LOCKFILE="/tmp/run_after_autologin.lock"
if [ -e "${LOCKFILE}" ]; then
    echo "Already run"
    exit 0
fi
touch "${LOCKFILE}"

# Detect installer disk
for DISK1 in $(list-devices disk); do
    if /usr/sbin/blkid "${DISK1}" | grep 'LABEL="Debian' | grep 'TYPE="iso9660"
'; then
        INSTALLER_DISK=${DISK1}
        break
    fi
done
echo "INSTALLER_DISK = ${INSTALLER_DISK}"

# Remove LV
LVS=$(sudo lvdisplay -C -o lv_path --readonly)
for LV1 in ${LVS}; do
    if [ x"${LV1}" = x"Path" ]; then
        continue
    fi
    echo "Removing LV ${LV1}"
    sudo lvremove "${LV1}" -f -y
done

# Remove VG
VGS=$(sudo vgdisplay -C -o vg_name --readonly)
for VG1 in ${VGS}; do
    if [ x"${VG1}" = x"VG" ]; then
        continue
    fi
    echo "Removing VG ${VG1}"
    sudo vgremove "${VG1}" -f -y
done

# Remove PV
PVS=$(sudo pvdisplay -C -o pv_name --readonly)
for PV1 in ${PVS}; do
    if [ x"${PV1}" = x"PV" ]; then
        continue
    fi
    if [[ x"${PV1}" == x"${INSTALLER_DISK}"* ]]; then
        continue
    fi
    sudo pvremove "${PV1}" -f -y
done

# Clear all partition tables
for DISK1 in $(list-devices disk); do
    if [ x"${DISK1}" = x"${INSTALLER_DISK}" ]; then
        continue
    else
        sudo sgdisk -Z "${DISK1}"
    fi
done

# Detect MAIN disk
for DISK2 in "/dev/nvme0n1" "/dev/sda" "/dev/vda"; do
    if [ x"${DISK2}" = x"${INSTALLER_DISK}" ]; then
        continue
    fi
    FOUNDFLAG=0
    for DISK1 in $(list-devices disk); do
        if [ x"${DISK1}" = x"${DISK2}" ]; then
            FOUNDFLAG=1
            break
        fi
    done
    if [ "${FOUNDFLAG}" -eq 1 ]; then
        MAIN_DISK="${DISK2}"
        break
    fi
done
echo "MAIN_DISK = ${MAIN_DISK}"

# Create Maindisk partition table
DEB_IMAGE="/usr/local/share/debian-images/debian-13-generic-arm64.raw"
EFI_PART_NUM=$(/usr/sbin/gdisk -l "${DEB_IMAGE}" | grep "EF00" | awk '{print $1}')
EFI_PART_START=$(partx -g -o START --nr "${EFI_PART_NUM}" "${DEB_IMAGE}")
EFI_PART_END=$(partx -g -o END --nr "${EFI_PART_NUM}" "${DEB_IMAGE}")
EFI_PART_SECTORS=$(partx -g -o SECTORS --nr "${EFI_PART_NUM}" "${DEB_IMAGE}")
EFI_PART_SIZE=$(partx -g -o SIZE --nr "${EFI_PART_NUM}" "${DEB_IMAGE}")
EFI_PART_PARTUUID=$(partx -g -o UUID --nr "${EFI_PART_NUM}" "${DEB_IMAGE}")
sudo sgdisk "-n:1:${EFI_PART_START}:+${EFI_PART_SIZE}" "-t:1:ef00" "-c:1:esp" \
    "-A:1:set:0" "-u:1:${EFI_PART_PARTUUID}" "${MAIN_DISK}"

PV_LIST=""
LVM_PART_START=$((EFI_PART_END + 1))
sudo sgdisk "-n:2:${LVM_PART_START}:0" "-t:2:8e00" "-c:2:pvlinarogigamprootfs0" "-A:2:set:0" "${MAIN_DISK}"

if [[ x"${MAIN_DISK}" == x/dev/nvme* ]]; then
    PV_LIST="${PV_LIST} ${MAIN_DISK}p2"
else
    PV_LIST="${PV_LIST} ${MAIN_DISK}2"
fi

# Create partition table for the rest of the disks
COUNTER=0
for DISK1 in $(list-devices disk); do
    COUNTER=$((COUNTER + 1))
    if [ x"${DISK1}" = x"${INSTALLER_DISK}" ]; then
        continue
    fi
    if [ x"${DISK1}" = x"${MAIN_DISK}" ]; then
        continue
    fi
    if [[ x"${MAIN_DISK}" == x/dev/nvme* && x"${DISK1}" == x/dev/nvme* ]]; then
        sudo sgdisk "-n:1:2048:0" "-t:1:8e00" "-c:1:pvlinarogigamprootfs${COUNTER}" "-A:1:set:0" "${DISK1}"
        PV_LIST="${PV_LIST} ${DISK1}p1"
    elif [[ x"${MAIN_DISK}" == x/dev/vd* && x"${DISK1}" == x/dev/vd* ]]; then
        sudo sgdisk "-n:1:2048:0" "-t:1:8e00" "-c:1:pvlinarogigamprootfs${COUNTER}" "-A:1:set:0" "${DISK1}"
        PV_LIST="${PV_LIST} ${DISK1}1"
    elif [[ x"${MAIN_DISK}" == x/dev/sd* && x"${DISK1}" == x/dev/sd* ]]; then
        sudo sgdisk "-n:1:2048:0" "-t:1:8e00" "-c:1:pvlinarogigamprootfs${COUNTER}" "-A:1:set:0" "${DISK1}"
        PV_LIST="${PV_LIST} ${DISK1}1"
    fi
done

# Create PV
for PV1 in ${PV_LIST}; do
    sudo pvcreate -f "${PV1}"
done

# Create VG
VGNAME="vglinarogigamprootfs"
sudo vgcreate "${VGNAME}" ${PV_LIST}

# Create LV
LVNAME="lvlinarogigamprootfs"
sudo lvcreate -l '100%FREE' -n "${LVNAME}" "${VGNAME}"

# Flash EFI partition
if [[ x"${MAIN_DISK}" == x/dev/nvme* ]]; then
    sudo dd if="${DEB_IMAGE}" of="${MAIN_DISK}p1" bs=512 skip=${EFI_PART_START} count=${EFI_PART_SECTORS}
else
    sudo dd if="${DEB_IMAGE}" of="${MAIN_DISK}1" bs=512 skip=${EFI_PART_START} count=${EFI_PART_SECTORS}
fi

# Flash rootfs
ROOTFS_PART_NUM="1"
ROOTFS_PART_START=$(partx -g -o START --nr "${ROOTFS_PART_NUM}" "${DEB_IMAGE}")
ROOTFS_PART_SECTORS=$(partx -g -o SECTORS --nr "${ROOTFS_PART_NUM}" "${DEB_IMAGE}")
sudo dd if="${DEB_IMAGE}" of="/dev/${VGNAME}/${LVNAME}" bs=512 skip=${ROOTFS_PART_START} count=${ROOTFS_PART_SECTORS}
sudo e2fsck -f "/dev/${VGNAME}/${LVNAME}"
sudo resize2fs "/dev/${VGNAME}/${LVNAME}"
sudo e2fsck -f "/dev/${VGNAME}/${LVNAME}"
sync
sudo shutdown -h 1
