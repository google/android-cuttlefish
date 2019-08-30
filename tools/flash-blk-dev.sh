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

image=$1
if [ "${image}" == "" ]; then
	echo "usage: "`basename $0`" <image>"
	exit 1
fi
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
sd_card=${new_devs}
# don't inform user we found the sd card yet (it's confusing)

init_devs=${devs}
echo "${init_devs}" | grep "${sd_card}" >/dev/null
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
			if [[ "${new_devs}" != "${sd_card}" ]]; then
				echo "error: block device name mismatch ${new_devs} != ${sd_card}"
				echo "Reinsert device (to write to) into PC"
				sd_card=${new_devs}
				new_devs=""
				continue
			fi
			break
		fi
	done
fi
# now inform the user
echo "Detected device at /dev/${sd_card}"

imgsize=`ls -lah ${image} | awk -F " " {'print $5'}`
echo "Ready to write ${imgsize} image to block device at /dev/${sd_card}..."
sudo chmod 666 /dev/${sd_card}
type pv > /dev/null 2>&1
if [ $? == 0 ]; then
	pv ${image} > /dev/${sd_card}
else
	dd if=${image} of=/dev/${sd_card} bs=1M conv=sync,noerror status=progress
fi

echo "Expanding partition and filesystem..."
part_type=`sudo gdisk -l /dev/${sd_card}  2>/dev/null | grep ": present" | sed 's/ *\([^:]*\):.*/\1/'`
if [ "$part_type" == "MBR" ]; then
	sudo parted -s /dev/${sd_card} resizepart 1 100%
	sudo e2fsck -y -f /dev/${sd_card}1 >/dev/null 2>&1
	sudo resize2fs /dev/${sd_card}1 >/dev/null 2>&1
elif [ "$part_type" == "GPT" ]; then
	parts=`sudo gdisk -l  /dev/${sd_card} | grep "^Number" -A999 | tail -n +2 | wc -l`
	FIRST_SECTOR=`sudo gdisk -l /dev/${sd_card} 2>/dev/null | tail -1 | tr -s ' ' | cut -d" " -f3`
	LAST_SECTOR=61071326  # 32GB eMMC size
	sudo sgdisk -d${parts} /dev/${sd_card} >/dev/null 2>&1
	sudo sgdisk -a1 -n:${parts}:${FIRST_SECTOR}:${LAST_SECTOR} -A:${parts}:set:2 -t:${parts}:8305 -c:${parts}:rootfs /dev/${sd_card} >/dev/null 2>&1
	sudo e2fsck -fy /dev/${sd_card}${parts} >/dev/null 2>&1
	sudo resize2fs /dev/${sd_card}${parts} >/dev/null 2>&1
fi
sudo sync /dev/${sd_card}
sudo eject /dev/${sd_card}

echo "Now insert the device into Rock Pi and plug in the USB power adapter"
