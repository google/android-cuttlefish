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

DEFINE_boolean p1 \
	false "Only generate/write the 1st partition (loader1)" "1"
DEFINE_boolean p2 \
	false "Only generate/write the 2nd partition (env)" "2"
DEFINE_boolean p3 \
	false "Only generate/write the 3rd partition (loader2)" "3"
DEFINE_boolean p4 \
	false "Only generate/write the 4th partition (trust)" "4"
DEFINE_boolean p5 \
	false "Only generate/write the 5th partition (rootfs)" "5"

UBOOT_REPO=
KERNEL_REPO=
IMAGE=

FLAGS_HELP="USAGE: $0 <UBOOT_REPO> <KERNEL_REPO> [IMAGE] [flags]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

if [ ${FLAGS_p1} -eq ${FLAGS_FALSE} ] &&
   [ ${FLAGS_p2} -eq ${FLAGS_FALSE} ] &&
   [ ${FLAGS_p3} -eq ${FLAGS_FALSE} ] &&
   [ ${FLAGS_p4} -eq ${FLAGS_FALSE} ] &&
   [ ${FLAGS_p5} -eq ${FLAGS_FALSE} ]; then
	FLAGS_p1=${FLAGS_TRUE}
	FLAGS_p2=${FLAGS_TRUE}
	FLAGS_p3=${FLAGS_TRUE}
	FLAGS_p4=${FLAGS_TRUE}
	FLAGS_p5=${FLAGS_TRUE}
fi

for arg in "$@" ; do
	if [ -z $UBOOT_REPO ]; then
		UBOOT_REPO=$arg
	elif [ -z $KERNEL_REPO ]; then
		KERNEL_REPO=$arg
	elif [ -z $IMAGE ]; then
		IMAGE=$arg
	else
		flags_help
		exit 1
	fi
done

USE_IMAGE=`[ -z "${IMAGE}" ] && echo "0" || echo "1"`
OVERWRITE=`[ -e "${IMAGE}" ] && echo "1" || echo "0"`
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
if [ $OVERWRITE -eq 1 ]; then
	OVERWRITE_IMAGE=${IMAGE}
	IMAGE=`mktemp`
fi

if [ $USE_IMAGE -eq 0 ]; then
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

if [ ${FLAGS_p2} -eq ${FLAGS_TRUE} ]; then
	tmpfile=`mktemp`
	bootenv=`mktemp`
	cat > ${tmpfile} << "EOF"
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
	echo "Sha=`${script_dir}/gen_sha.sh --uboot ${UBOOT_REPO} --kernel ${KERNEL_REPO}`" >> ${tmpfile}
	${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_tools/mkenvimage -s 32768 -o ${bootenv} - < ${tmpfile}
fi

if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ] || [ ${FLAGS_p3} -eq ${FLAGS_TRUE} ]; then
	cd ${UBOOT_REPO}
	BUILD_CONFIG=u-boot/build.config.rockpi4 build/build.sh -j1
	cd -
fi

if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
	cd ${KERNEL_REPO}
	rm -rf out
	BUILD_CONFIG=common/build.config.rockpi4 build/build.sh -j`nproc`
	cd -

	dist_dir=$(echo ${KERNEL_REPO}/out/android*/dist)
	${ANDROID_BUILD_TOP}/kernel/tests/net/test/build_rootfs.sh \
		-a arm64 -s bullseye-rockpi -n ${IMAGE} -r ${IMAGE}.initrd -e \
		-k ${dist_dir}/Image -i ${dist_dir}/initramfs.img \
		-d ${dist_dir}/rk3399-rock-pi-4b.dtb:rockchip
	if [ $? -ne 0 ]; then
		echo "error: failed to build rootfs. exiting..."
		exit 1
	fi
	rm -f ${IMAGE}.initrd
	truncate -s +3G ${IMAGE}
	e2fsck -f ${IMAGE}
	resize2fs ${IMAGE}

	# Turn on journaling
	tune2fs -O ^has_journal ${IMAGE}
	e2fsck -fy ${IMAGE} >/dev/null 2>&1
fi

if [ ${USE_IMAGE} -eq 0 ]; then
	device=/dev/${mmc_dev}
	devicep=${device}

	# 32GB eMMC size
	end_sector=61071326

	sudo sgdisk --zap-all --set-alignment=1 ${device}
	sudo sgdisk --set-alignment=1 --new=1:64:8127 --typecode=1:8301 --change-name=1:loader1 ${device}
	sudo sgdisk --set-alignment=1 --new=2:8128:8191 --typecode=2:8301 --change-name=2:env ${device}
	sudo sgdisk --set-alignment=1 --new=3:16384:24575 --typecode=3:8301 --change-name=3:loader2 ${device}
	sudo sgdisk --set-alignment=1 --new=4:24576:32767 --typecode=4:8301 --change-name=4:trust ${device}
	sudo sgdisk --set-alignment=1 --new=5:32768:${end_sector} --typecode=5:8305 --change-name=5:rootfs --attributes=5:set:2 ${device}
	if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
		sudo dd if=${IMAGE} of=${devicep}5 bs=1M conv=fsync
		sudo resize2fs ${devicep}5 >/dev/null 2>&1
	fi
else
	device=$(sudo losetup -f)
	devicep=${device}p

	if [ ${FLAGS_p5} -eq ${FLAGS_FALSE} ]; then
		fs_end=3G
		end_sector=-
	fi
	if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
		# Minimize rootfs filesystem
		while true; do
			out=`sudo resize2fs -M ${IMAGE} 2>&1`
			if [[ $out =~ "Nothing to do" ]]; then
				break
			fi
		done
		# Minimize rootfs file size
		block_count=`sudo tune2fs -l ${IMAGE} | grep "Block count:" | sed 's/.*: *//'`
		block_size=`sudo tune2fs -l ${IMAGE} | grep "Block size:" | sed 's/.*: *//'`
		sector_size=512
		start_sector=32768
		fs_size=$(( block_count*block_size ))
		fs_sectors=$(( fs_size/sector_size ))
		part_sectors=$(( ((fs_sectors-1)/2048+1)*2048 ))  # 1MB-aligned
		end_sector=$(( start_sector+part_sectors-1 ))
		secondary_gpt_sectors=33
		fs_end=$(( (end_sector+secondary_gpt_sectors+1)*sector_size ))
		image_size=$(( part_sectors*sector_size ))
		truncate -s ${image_size} ${IMAGE}
		e2fsck -fy ${IMAGE} >/dev/null 2>&1
	fi

	# Create final image
	if [ $OVERWRITE -eq 1 ]; then
		tmpimg=${OVERWRITE_IMAGE}
	else
		tmpimg=`mktemp`
	fi
	truncate -s ${fs_end} ${tmpimg}

	# Create GPT
	sgdisk --zap-all --set-alignment=1 ${tmpimg}
	sgdisk --set-alignment=1 --new=1:64:8127 --typecode=1:8301 --change-name=1:loader1 ${tmpimg}
	sgdisk --set-alignment=1 --new=2:8128:8191 --typecode=2:8301 --change-name=2:env ${tmpimg}
	sgdisk --set-alignment=1 --new=3:16384:24575 --typecode=3:8301 --change-name=3:loader2 ${tmpimg}
	sgdisk --set-alignment=1 --new=4:24576:32767 --typecode=4:8301 --change-name=4:trust ${tmpimg}
	sgdisk --set-alignment=1 --new=5:32768:${end_sector} --typecode=5:8305 --change-name=5:rootfs --attributes=5:set:2 ${tmpimg}

	sudo losetup ${device} ${tmpimg}
	sudo partx -v --add ${device}

	if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
		sudo dd if=${IMAGE} of=${devicep}5 bs=1M conv=fsync
	fi
fi
if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ]; then
	# sudo dd if=${UBOOT_REPO}/out/u-boot-mainline/dist/idbloader.img of=${devicep}1 conv=fsync
	# loader1
	sudo dd if=${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_bin/idbloader.img of=${devicep}1 conv=fsync
fi
if [ ${FLAGS_p2} -eq ${FLAGS_TRUE} ]; then
	sudo dd if=${bootenv} of=${devicep}2 conv=fsync
fi
if [ ${FLAGS_p3} -eq ${FLAGS_TRUE} ]; then
	# sudo dd if=${UBOOT_REPO}/out/u-boot-mainline/dist/u-boot.itb of=${devicep}3 conv=fsync
	# loader2
	sudo dd if=${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts/uboot_bin/u-boot.itb of=${devicep}3 conv=fsync
fi
if [ ${USE_IMAGE} -eq 1 ]; then
	sudo partx -v --delete ${device}
	sudo losetup -d ${device}
	if [ $OVERWRITE -eq 0 ]; then
		mv ${tmpimg} ${IMAGE}
	fi
fi
