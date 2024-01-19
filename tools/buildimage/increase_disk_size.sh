#!/bin/bash

# Copyright (C) 2024 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Increases image's disk size increasing the partition `1` size.

set -e

usage() {
  echo "usage: $0 -r /path/to/disk.raw"
}

diskraw=

# Defaults to a final image size of 20.00GiB. Original image has a size of 2.00GiB.
# TODO: Make the increase size value an option.
inc_in_gib=18

while getopts ":hr:i:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    r)
      diskraw="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      usage
      exit 1
      ;;
  esac
done

echo -e "Original partition table:"
sudo parted ${diskraw} unit GiB print free
echo "Appending ${inc_in_gib}GiB of null characters making the required space ..."
dd if=/dev/zero bs=1G count=${inc_in_gib} >> ${diskraw}
# Answer "Fix" to the next prompt when using `parted`.
# Warning: Not all of the space available to ${diskraw} appears to be used, you can fix the GPT to use all of the space (an extra 2014 blocks) or continue with the current setting?
# Fix/Ignore?
printf "fix\n" | sudo parted ---pretend-input-tty ${diskraw} unit B print free
new_end=$(sudo parted -s ${diskraw} unit B print free 2>/dev/null | awk '/Free Space$/ {print $2}' | tail -n 1)
echo "Resizing partition ..."
sudo parted ${diskraw} resizepart 1 ${new_end} 1> /dev/null
sudo parted ${diskraw} unit GiB print free
# Update the filesystem table catching up with new size
sudo kpartx -a ${diskraw}
loopdev=$(sudo losetup -l | awk -v pat="${diskraw}" '$0~pat {print $1}')
mapperdev=/dev/mapper/$(basename -- "$loopdev")p1
sudo e2fsck -f -y -v -C 0 ${mapperdev}
sudo resize2fs -p ${mapperdev}
sudo kpartx -d ${diskraw}

