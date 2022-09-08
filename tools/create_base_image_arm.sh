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

UBOOT_REPO=
KERNEL_REPO=
OUTPUT_IMAGE=

FLAGS_HELP="USAGE: $0 <UBOOT_REPO> <KERNEL_REPO> [OUTPUT_IMAGE] [flags]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

for arg in "$@" ; do
	if [ -z $UBOOT_REPO ]; then
		UBOOT_REPO=$arg
	elif [ -z $KERNEL_REPO ]; then
		KERNEL_REPO=$arg
	elif [ -z $OUTPUT_IMAGE ]; then
		OUTPUT_IMAGE=$arg
	else
		flags_help
		exit 1
	fi
done

if [ -z $KERNEL_REPO -o -z $UBOOT_REPO ]; then
	flags_help
	exit 1
fi
if [ ! -e "${UBOOT_REPO}" ]; then
	echo "error: can't find '${UBOOT_REPO}'. aborting..."
	exit 1
fi
if [ ! -e "${KERNEL_REPO}" ]; then
	echo "error: can't find '${KERNEL_REPO}'. aborting..."
	exit 1
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
EOF
echo "Sha=`${script_dir}/gen_sha.sh --uboot ${UBOOT_REPO} --kernel ${KERNEL_REPO}`" >> ${bootenv_src}
${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_tools/mkenvimage -s 32768 -o ${bootenv} - < ${bootenv_src}
rm -f ${bootenv_src}

cd ${UBOOT_REPO}
BUILD_CONFIG=u-boot/build.config.rockpi4 build/build.sh -j1
cd -

cd ${KERNEL_REPO}
rm -rf out
BUILD_CONFIG=common/build.config.rockpi4 build/build.sh -j`nproc`
cd -

IMAGE=`mktemp`
kernel_dist_dir=$(echo ${KERNEL_REPO}/out/android*/dist)
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
	device=$(sudo losetup -f)
	devicep=${device}p

	# Set up loop device for whole disk image
	sudo losetup -P ${device} ${IMAGE}

	# Minimize rootfs filesystem
	sudo e2fsck -fy ${devicep}7 >/dev/null 2>&1
	while true; do
		out=`sudo resize2fs -M ${devicep}7 2>&1`
		if [[ $out =~ "Nothing to do" ]]; then
			break
		fi
	done
	# Minimize rootfs file size
	block_count=`sudo tune2fs -l ${devicep}7 | grep "Block count:" | sed 's/.*: *//'`
	block_size=`sudo tune2fs -l ${devicep}7 | grep "Block size:" | sed 's/.*: *//'`
	sector_size=512
	start_sector=296960
	fs_size=$(( block_count*block_size ))
	fs_sectors=$(( fs_size/sector_size ))
	part_sectors=$(( ((fs_sectors-1)/2048+1)*2048 ))  # 1MB-aligned
	end_sector=$(( start_sector+part_sectors-1 ))
	secondary_gpt_sectors=33
	fs_end=$(( (end_sector+secondary_gpt_sectors+1)*sector_size ))
	image_size=$(( part_sectors*sector_size ))

        # Disable ext3/4 journal for flashing to SD-Card
	sudo tune2fs -O ^has_journal ${devicep}7
	sudo e2fsck -fy ${devicep}7 >/dev/null 2>&1

	# Update partition table
	sudo sgdisk --delete=7 ${device}
	sudo sgdisk --new=7:145M:${end_sector} --typecode=7:8305 --change-name=7:rootfs --attributes=7:set:2 ${device}
fi

# idbloader
sudo dd if=${UBOOT_REPO}/out/u-boot-mainline/dist/idbloader.img of=${devicep}1 conv=fsync
# prebuilt
# sudo dd if=${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_bin/idbloader.img of=${devicep}1 conv=fsync

# uboot_env
sudo dd if=${bootenv} of=${devicep}2 conv=fsync

# uboot
sudo dd if=${UBOOT_REPO}/out/u-boot-mainline/dist/u-boot.itb of=${devicep}3 conv=fsync
# prebuilt
# sudo dd if=${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_bin/u-boot.itb of=${devicep}3 conv=fsync

if [ ${WRITE_TO_IMAGE} -eq 1 ]; then
	sudo losetup -d ${device}
	truncate -s ${fs_end} ${IMAGE}
	sgdisk --move-second-header ${IMAGE}
	mv -f ${IMAGE} ${OUTPUT_IMAGE}
fi
