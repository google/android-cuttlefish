#!/bin/bash

# Copyright 2022 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


set -x
set -u
# comment out -e because partx has error message
# set -o errexit
# set -e

# get script directory
script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ -z $ANDROID_BUILD_TOP ]; then
  echo "error: run script after 'lunch'"
  exit 1
fi

source "${ANDROID_BUILD_TOP}/external/shflags/shflags"

# prepare pre-requested files, including kernel and uboot
# get temp directory
tmpdir=`echo $RANDOM | md5sum | head -c 20`
mkdir "/tmp/$tmpdir"
UBOOT_REPO="/tmp/$tmpdir/uboot"
KERNEL_REPO="/tmp/$tmpdir/kernel"
IMAGE="/tmp/$tmpdir/test_image"
ARCH=
FLAGS_HELP="USAGE: $0 [ARCH]"
mkdir $UBOOT_REPO
mkdir $KERNEL_REPO
cd $KERNEL_REPO
repo init -u persistent-https://android.git.corp.google.com/kernel/manifest -b common-android13-5.15 && repo sync

# parse input parameters
FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

for arg in "$@" ; do
  if [ -z $ARCH ]; then
    ARCH=$arg
  else
    flags_help
    exit 1
  fi
done

if [[ "$ARCH" == "arm" ]]; then
  cd $UBOOT_REPO
  repo init -u persistent-https://android.git.corp.google.com/kernel/manifest -b u-boot-mainline && repo sync
fi

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

# build uboot based on architecture
cd ${UBOOT_REPO}
if [[ "$ARCH" == "arm" ]]; then
  BUILD_CONFIG=u-boot/build.config.rockpi4 build/build.sh -j1
fi
cd -

# build kernel based on architecture
cd ${KERNEL_REPO}
rm -rf out
if [[ "$ARCH" == "arm" ]]; then
  BUILD_CONFIG=common/build.config.rockpi4 build/build.sh -j`nproc`
else
  BUILD_CONFIG=common/build.config.gce.x86_64 build/build.sh -j`nproc`
fi
cd -

dist_dir=$(echo ${KERNEL_REPO}/out*/dist)

# build rootfs/host images
if [[ "$ARCH" == "arm" ]]; then
  ${ANDROID_BUILD_TOP}/kernel/tests/net/test/build_rootfs.sh \
    -a arm64 -s bullseye-rockpi -n ${IMAGE} -r ${IMAGE}.initrd -e \
    -k ${dist_dir}/Image -i ${dist_dir}/initramfs.img \
    -d ${dist_dir}/rk3399-rock-pi-4b.dtb:rockchip
else
  ${ANDROID_BUILD_TOP}/kernel/tests/net/test/build_rootfs.sh \
    -a amd64 -s bullseye-server -n ${IMAGE} -r ${IMAGE}.initrd -e \
    -k ${dist_dir}/bzImage -i ${dist_dir}/initramfs.img -g
fi

if [ $? -ne 0 ]; then
  echo "error: failed to build rootfs. exiting..."
  exit 1
fi

rm -f ${IMAGE}.initrd
rm -rf ${ANDROID_BUILD_TOP}/disk.raw
cp ${IMAGE} ${ANDROID_BUILD_TOP}/disk.raw
cd ${ANDROID_BUILD_TOP}
rm -rf disk_${USER}.raw.tar.gz
tar Szcvf disk_${USER}.raw.tar.gz disk.raw
