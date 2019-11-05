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

usage() {
	echo "USAGE: $0 [flags] image"
	echo "flags:"
	echo "  -e,--expand:  expand filesystem to fill device (default: false)"
	echo "  -h,--help:    show this help (default: false)"
}

main()
{
	if [ ! -e "${image}" ]; then
		echo "error: can't find image. aborting..."
		exit 1
	fi

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
			break
		fi
	done
	blk_dev=${new_devs}
	# don't inform user we found the block device yet (it's confusing)

	init_devs=${devs}
	echo "${init_devs}" | grep "${blk_dev}" >/dev/null
	if [[ $? -ne 0 ]]; then
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
				if [[ "${new_devs}" != "${blk_dev}" ]]; then
					echo "error: block device name mismatch ${new_devs} != ${blk_dev}"
					echo "Reinsert device (to write to) into PC"
					blk_dev=${new_devs}
					new_devs=""
					continue
				fi
				break
			fi
		done
	fi
	# now inform the user
	echo "Detected device at /dev/${blk_dev}"

	imgsize=`ls -lah ${image} | awk -F " " {'print $5'}`
	echo "Ready to write ${imgsize} image to block device at /dev/${blk_dev}..."
	sudo chmod 666 /dev/${blk_dev}
	type pv > /dev/null 2>&1
	if [ $? == 0 ]; then
		pv ${image} > /dev/${blk_dev}
	else
		dd if=${image} of=/dev/${blk_dev} bs=1M conv=sync,noerror status=progress
	fi
	if [ $? != 0 ]; then
		echo "error: failed to write to device. aborting..."
		exit 1
	fi

	if [ ${expand} -eq 1 ]; then
		echo "Expanding partition and filesystem..."
		part_type=`sudo gdisk -l /dev/${blk_dev}  2>/dev/null | grep ": present" | sed 's/ *\([^:]*\):.*/\1/'`
		if [ "$part_type" == "MBR" ]; then
			sudo parted -s /dev/${blk_dev} resizepart 1 100%
			sudo e2fsck -y -f /dev/${blk_dev}1 >/dev/null 2>&1
			sudo resize2fs /dev/${blk_dev}1 >/dev/null 2>&1
		elif [ "$part_type" == "GPT" ]; then
			parts=`sudo gdisk -l  /dev/${blk_dev} | grep "^Number" -A999 | tail -n +2 | wc -l`
			FIRST_SECTOR=`sudo gdisk -l /dev/${blk_dev} 2>/dev/null | tail -1 | tr -s ' ' | cut -d" " -f3`
			sudo sgdisk -d${parts} /dev/${blk_dev} >/dev/null 2>&1
			sudo sgdisk -a1 -n:${parts}:${FIRST_SECTOR}:- -A:${parts}:set:2 -t:${parts}:8305 -c:${parts}:rootfs /dev/${blk_dev} >/dev/null 2>&1
			sudo e2fsck -fy /dev/${blk_dev}${parts} >/dev/null 2>&1
			sudo resize2fs /dev/${blk_dev}${parts} >/dev/null 2>&1
		fi
	fi
	sudo sync /dev/${blk_dev}
	sudo eject /dev/${blk_dev}
}

expand=0
if [ "$1" == "-e" ] || [ "$1" == "--expand" ]; then
	expand=1
	shift
fi

if [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
	usage
	exit 0
fi

image=$1
if [ "${image}" == "" ]; then
	usage
	exit 1
fi

main "$@"
