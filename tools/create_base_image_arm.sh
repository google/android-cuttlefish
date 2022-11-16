#!/bin/bash

# Copyright 2019 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e
set -u

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ -z $ANDROID_BUILD_TOP ]; then
	echo "error: run script after 'lunch'"
	exit 1
fi

source "${ANDROID_BUILD_TOP}/external/shflags/shflags"

UBOOT_DIST=
KERNEL_DIST=
OUTPUT_IMAGE=

FLAGS_HELP="USAGE: $0 [UBOOT_DIST] [KERNEL_DIST] [OUTPUT_IMAGE] [flags]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

for arg in "$@" ; do
	if [ -z $UBOOT_DIST ]; then
		UBOOT_DIST=$arg
	elif [ -z $KERNEL_DIST ]; then
		KERNEL_DIST=$arg
	elif [ -z $OUTPUT_IMAGE ]; then
		OUTPUT_IMAGE=$arg
	else
		flags_help
		exit 1
	fi
done

if [ -z "${KERNEL_DIST}" ]; then
	OUTPUT_IMAGE="${UBOOT_DIST}"
	UBOOT_DIST=
fi

if [ ! -e "${UBOOT_DIST}" ]; then
	echo "No UBOOT_DIST, use prebuilts"
	UBOOT_DIST="${ANDROID_BUILD_TOP}"/device/google/cuttlefish_prebuilts/bootloader/rockpi_aarch64
fi
if [ ! -e "${KERNEL_DIST}" ]; then
	echo "No KERNEL_DIST, use prebuilts"
	KERNEL_DIST=$(find "${ANDROID_BUILD_TOP}"/device/google/cuttlefish_prebuilts/kernel -name '*-arm64-rockpi' | sort | tail -n 1)
fi

WRITE_TO_IMAGE=`[ -z "${OUTPUT_IMAGE}" ] && echo "0" || echo "1"`

if [ $WRITE_TO_IMAGE -eq 0 ]; then
	init_devs=`lsblk --nodeps -oNAME -n`
	echo "Reinsert device (to write to) into PC"
	while true; do
		devs=`lsblk --nodeps -oNAME -n`
		new_devs="$(echo -e "${init_devs}\n${devs}" | sort | uniq -u | awk 'NF')"
		num_devs=`echo "${new_devs}" | wc -l`
		if [[ "${new_devs}" == "" ]]; then
			num_devs=0
		fi
		if [[ ${num_devs} -gt 1 ]]; then
			echo "error: too many new devices detected! aborting..."
			exit 1
		fi
		if [[ ${num_devs} -eq 1 ]]; then
			if [[ "${new_devs}" != "${mmc_dev}" ]]; then
				if [[ "${mmc_dev}" != "" ]]; then
					echo "error: block device name mismatch ${new_devs} != ${mmc_dev}"
					echo "Reinsert device (to write to) into PC"
				fi
				mmc_dev=${new_devs}
				continue
			fi
			echo "${init_devs}" | grep "${mmc_dev}" >/dev/null
			if [[ $? -eq 0 ]]; then
				init_devs="${devs}"
				continue
			fi
			break
		fi
	done
	# now inform the user
	echo "Detected device at /dev/${mmc_dev}"
fi

bootenv_src=`mktemp`
bootenv=`mktemp`
cat > ${bootenv_src} << "EOF"
bootdelay=2
baudrate=1500000
scriptaddr=0x00500000
boot_targets=mmc1 mmc0
bootcmd=run distro_bootcmd
distro_bootcmd=for target in ${boot_targets}; do run bootcmd_${target}; done
bootcmd_mmc0=devnum=0; run mmc_boot
bootcmd_mmc1=devnum=1; run mmc_boot
mmc_boot=if mmc dev ${devnum}; then ; run scan_for_boot_part; fi
scan_for_boot_part=part list mmc ${devnum} -bootable devplist; env exists devplist || setenv devplist 1; for distro_bootpart in ${devplist}; do if fstype mmc ${devnum}:${distro_bootpart} bootfstype; then run find_script; fi; done; setenv devplist;
find_script=if test -e mmc ${devnum}:${distro_bootpart} /boot/boot.scr; then echo Found U-Boot script /boot/boot.scr; run run_scr; fi
run_scr=load mmc ${devnum}:${distro_bootpart} ${scriptaddr} /boot/boot.scr; source ${scriptaddr}
fastboot_raw_partition_raw1=0x0 0x2000000
EOF
echo "Sha=`${script_dir}/gen_sha.sh --uboot ${UBOOT_DIST} --kernel ${KERNEL_DIST}`" >> ${bootenv_src}
${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_tools/mkenvimage -s 32768 -o ${bootenv} - < ${bootenv_src}
rm -f ${bootenv_src}

IMAGE=`mktemp`
kernel_dist_dir=$(echo ${KERNEL_DIST})
kernel_dist_dir=$(realpath -e ${kernel_dist_dir})
${ANDROID_BUILD_TOP}/kernel/tests/net/test/build_rootfs.sh \
	-a arm64 -s bullseye-rockpi -n ${IMAGE} -r ${IMAGE}.initrd -e -g \
	-k ${kernel_dist_dir}/Image -i ${kernel_dist_dir}/initramfs.img \
	-d ${kernel_dist_dir}/rk3399-rock-pi-4b.dtb:rockchip
if [ $? -ne 0 ]; then
	echo "error: failed to build rootfs. exiting..."
	rm -f ${IMAGE}
	exit 1
fi
rm -f ${IMAGE}.initrd

if [ ${WRITE_TO_IMAGE} -eq 0 ]; then
	device=/dev/${mmc_dev}
	devicep=${device}

	# Burn the whole disk image with partition table
	sudo dd if=${IMAGE} of=${device} bs=1M conv=fsync

	# Update partition table for 32GB eMMC
	end_sector=61071326
	sudo sgdisk --delete=7 ${device}
	sudo sgdisk --new=7:145M:${end_sector} --typecode=7:8305 --change-name=7:rootfs --attributes=7:set:2 ${device}

	# Rescan the partition table and resize the rootfs
	sudo partx -v --add ${device}
	sudo resize2fs ${devicep}7 >/dev/null 2>&1
else
	# Minimize rootfs filesystem
	rootfs_partition_start=$(partx -g -o START -s -n 7 "${IMAGE}" | xargs)
	rootfs_partition_end=$(partx -g -o END -s -n 7 "${IMAGE}" | xargs)
	rootfs_partition_num_sectors=$((${rootfs_partition_end} - ${rootfs_partition_start} + 1))
	rootfs_partition_offset=$((${rootfs_partition_start} * 512))
	rootfs_partition_size=$((${rootfs_partition_num_sectors} * 512))
	e2fsck -fy ${IMAGE}?offset=${rootfs_partition_offset} >/dev/null 2>&1
	imagesize=`stat -c %s "${IMAGE}"`
	loopdev_rootfs="$(sudo losetup -f)"
	sudo losetup --offset ${rootfs_partition_offset} --sizelimit ${rootfs_partition_size} "${loopdev_rootfs}" "${IMAGE}"
	while true; do
		out=`sudo resize2fs -M ${loopdev_rootfs} 2>&1`
		if [[ $out =~ "Nothing to do" ]]; then
			break
		fi
	done
	sudo losetup -d ${loopdev_rootfs}
	truncate -s "${imagesize}" "${IMAGE}"
	sgdisk -e "${IMAGE}"
	e2fsck -fy ${IMAGE}?offset=${rootfs_partition_offset} || true

	# Minimize rootfs file size
	block_count=`tune2fs -l ${IMAGE}?offset=${rootfs_partition_offset} | grep "Block count:" | sed 's/.*: *//'`
	block_size=`tune2fs -l ${IMAGE}?offset=${rootfs_partition_offset} | grep "Block size:" | sed 's/.*: *//'`
	sector_size=512
	start_sector=`partx -g -o START -s -n 7 "${IMAGE}" | xargs`
	fs_size=$(( block_count*block_size ))
	fs_sectors=$(( fs_size/sector_size ))
	part_sectors=$(( ((fs_sectors-1)/2048+1)*2048 ))  # 1MB-aligned
	end_sector=$(( start_sector+part_sectors-1 ))
	secondary_gpt_sectors=33
	fs_end=$(( (end_sector+secondary_gpt_sectors+1)*sector_size ))
	image_size=$(( part_sectors*sector_size ))

        # Disable ext3/4 journal for flashing to SD-Card
	tune2fs -O ^has_journal ${IMAGE}?offset=${rootfs_partition_offset}
	e2fsck -fy ${IMAGE}?offset=${rootfs_partition_offset} >/dev/null 2>&1

	# Update partition table
	sgdisk --delete=7 ${IMAGE}
	sgdisk --new=7:145M:${end_sector} --typecode=7:8305 --change-name=7:rootfs --attributes=7:set:2 ${IMAGE}
fi

# idbloader
if [ ${WRITE_TO_IMAGE} -eq 0 ]; then
	sudo dd if=${UBOOT_DIST}/idbloader.img of=${devicep}1 conv=fsync
else
	idbloader_partition_start=$(partx -g -o START -s -n 1 "${IMAGE}" | xargs)
	dd if=${UBOOT_DIST}/idbloader.img of="${IMAGE}" bs=512 seek=${idbloader_partition_start} conv=fsync,notrunc
fi
# prebuilt
# sudo dd if=${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_bin/idbloader.img of=${devicep}1 conv=fsync

# uboot_env
if [ ${WRITE_TO_IMAGE} -eq 0 ]; then
	sudo dd if=${bootenv} of=${devicep}2 conv=fsync
else
	ubootenv_partition_start=$(partx -g -o START -s -n 2 "${IMAGE}" | xargs)
	dd if=${bootenv} of="${IMAGE}" bs=512 seek=${ubootenv_partition_start} conv=fsync,notrunc
fi
# uboot
if [ ${WRITE_TO_IMAGE} -eq 0 ]; then
	sudo dd if=${UBOOT_DIST}/u-boot.itb of=${devicep}3 conv=fsync
else
	uboot_partition_start=$(partx -g -o START -s -n 3 "${IMAGE}" | xargs)
	dd if=${UBOOT_DIST}/u-boot.itb of="${IMAGE}" bs=512 seek=${uboot_partition_start} conv=fsync,notrunc
fi
# prebuilt
# sudo dd if=${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_bin/u-boot.itb of=${devicep}3 conv=fsync

if [ ${WRITE_TO_IMAGE} -eq 1 ]; then
	truncate -s ${fs_end} ${IMAGE}
	sgdisk --move-second-header ${IMAGE}
	mv -f ${IMAGE} ${OUTPUT_IMAGE}
fi
