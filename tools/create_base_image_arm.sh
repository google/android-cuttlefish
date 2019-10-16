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

source "${ANDROID_BUILD_TOP}/external/shflags/src/shflags"

FLAGS_HELP="USAGE: $0 <KERNEL_DIR> [IMAGE]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

for arg in "$@" ; do
	if [ -z $KERNEL_DIR ]; then
		KERNEL_DIR=$arg
	elif [ -z $IMAGE ]; then
		IMAGE=$arg
	else
		flags_help
		exit 1
	fi
done

USE_IMAGE=`[ -z "${IMAGE}" ] && echo "0" || echo "1"`
if [ -z $KERNEL_DIR ] || [ -z $IMAGE ]; then
	flags_help
	exit 1
fi
if [ -e "${IMAGE}" ]; then
	echo "error: ${IMAGE} already exists"
	exit 1
fi
if [ ! -e "${KERNEL_DIR}" ]; then
	echo "error: can't find '${KERNEL_DIR}'. aborting..."
	exit 1
fi

# escalate to superuser
if [ $UID -ne 0 ]; then
	cd ${ANDROID_BUILD_TOP}
	. ./build/envsetup.sh
	lunch ${TARGET_PRODUCT}-${TARGET_BUILD_VARIANT}
	mmma external/u-boot
	cd -
	exec sudo -E "${0}" ${@}
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

cd ${ANDROID_BUILD_TOP}/external/arm-trusted-firmware
CROSS_COMPILE=aarch64-linux-gnu- make PLAT=rk3399 DEBUG=0 ERROR_DEPRECATED=1 bl31
export BL31="${ANDROID_BUILD_TOP}/external/arm-trusted-firmware/build/rk3399/release/bl31/bl31.elf"
cd -

idbloader=`mktemp`
cd ${ANDROID_BUILD_TOP}/external/u-boot
make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- rock-pi-4-rk3399_defconfig
make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- -j`nproc`
${ANDROID_HOST_OUT}/bin/mkimage -n rk3399 -T rksd -d tpl/u-boot-tpl.bin ${idbloader}
cat spl/u-boot-spl.bin >> ${idbloader}
cd -

${ANDROID_BUILD_TOP}/kernel/tests/net/test/build_rootfs.sh -a arm64 -s buster -n ${IMAGE}
if [ $? -ne 0 ]; then
	echo "error: failed to build rootfs. exiting..."
	exit 1
fi
truncate -s +3G ${IMAGE}
e2fsck -f ${IMAGE}
resize2fs ${IMAGE}

mntdir=`mktemp -d`
mount ${IMAGE} ${mntdir}
if [ $? != 0 ]; then
	echo "error: unable to mount ${IMAGE} ${mntdir}"
	exit 1
fi

cat > ${mntdir}/boot/boot.cmd << "EOF"
load mmc ${devnum}:${distro_bootpart} 0x02080000 /boot/Image
load mmc ${devnum}:${distro_bootpart} 0x04000000 /boot/uInitrd
load mmc ${devnum}:${distro_bootpart} 0x01f00000 /boot/dtb/rockchip/rk3399-rock-pi-4.dtb
setenv finduuid "part uuid mmc ${devnum}:${distro_bootpart} uuid"
run finduuid
setenv bootargs "earlycon=uart8250,mmio32,0xff1a0000 console=ttyS2,1500000n8 loglevel=7 root=PARTUUID=${uuid} rootwait rootfstype=ext4 sdhci.debug_quirks=0x20000000"
booti 0x02080000 0x04000000 0x01f00000
EOF
${ANDROID_HOST_OUT}/bin/mkimage \
	-C none -A arm -T script -d ${mntdir}/boot/boot.cmd ${mntdir}/boot/boot.scr

cd ${KERNEL_DIR}
export PATH=${ANDROID_BUILD_TOP}/prebuilts/clang/host/linux-x86/clang-r353983c/bin:$PATH
export PATH=${ANDROID_BUILD_TOP}/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin:$PATH
make ARCH=arm64 CC=clang CROSS_COMPILE=aarch64-linux-androidkernel- \
      CLANG_TRIPLE=aarch64-linux-gnu- rockpi4_defconfig
make ARCH=arm64 CC=clang CROSS_COMPILE=aarch64-linux-androidkernel- \
      CLANG_TRIPLE=aarch64-linux-gnu- -j`nproc`

cp ${KERNEL_DIR}/arch/arm64/boot/Image ${mntdir}/boot/
mkdir -p ${mntdir}/boot/dtb/rockchip/
cp ${KERNEL_DIR}/arch/arm64/boot/dts/rockchip/rk3399-rock-pi-4.dtb ${mntdir}/boot/dtb/rockchip/
cd -

mount -o bind /proc ${mntdir}/proc
mount -o bind /sys ${mntdir}/sys
mount -o bind /dev ${mntdir}/dev

echo "Installing required packages..."
chroot ${mntdir} /bin/bash <<EOF
apt-get update
apt-get install -y -f initramfs-tools u-boot-tools network-manager openssh-server sudo man-db vim git dpkg-dev cdbs debhelper config-package-dev gdisk eject lzop binfmt-support ntpdate
EOF

echo "Turning on DHCP client..."
cat >${mntdir}/etc/systemd/network/dhcp.network <<EOF
[Match]
Name=en*

[Network]
DHCP=yes
EOF

chroot ${mntdir} /bin/bash << "EOT"
echo "Adding user vsoc-01 and groups..."
useradd -m -G kvm,sudo -d /home/vsoc-01 --shell /bin/bash vsoc-01
echo -e "cuttlefish\ncuttlefish" | passwd
echo -e "cuttlefish\ncuttlefish" | passwd vsoc-01
EOT

echo "Cloning android-cuttlefish..."
cd ${mntdir}/home/vsoc-01
git clone https://github.com/google/android-cuttlefish.git
cd -

echo "Creating led script..."
cat > ${mntdir}/usr/local/bin/led << "EOF"
#!/bin/bash

if [ "$1" == "--start" ]; then
  echo 125 > /sys/class/gpio/export
  echo out > /sys/class/gpio/gpio125/direction
  chmod 666 /sys/class/gpio/gpio125/value
  echo 0 > /sys/class/gpio/gpio125/value
  exit 0
fi

if [ "$1" == "--stop" ]; then
  echo 0 > /sys/class/gpio/gpio125/value
  echo 125 > /sys/class/gpio/unexport
  exit 0
fi

if [ ! -e /sys/class/gpio/gpio125/value ]; then
  echo "error: led service not initialized"
  exit 1
fi

if [ "$1" == "0" ] || [ "$1" == "off" ] || [ "$1" == "OFF" ]; then
  echo 0 > /sys/class/gpio/gpio125/value
  exit 0
fi

if [ "$1" == "1" ] || [ "$1" == "on" ] || [ "$1" == "ON" ]; then
  echo 1 > /sys/class/gpio/gpio125/value
  exit 0
fi

echo "usage: led <0|1>"
exit 1
EOF
chown root:root ${mntdir}/usr/local/bin/led
chmod 755 ${mntdir}/usr/local/bin/led

echo "Creating led service..."
cat > ${mntdir}/etc/systemd/system/led.service << EOF
[Unit]
 Description=led service
 ConditionPathExists=/usr/local/bin/led

[Service]
 Type=oneshot
 ExecStart=/usr/local/bin/led --start
 ExecStop=/usr/local/bin/led --stop
 RemainAfterExit=true
 StandardOutput=journal

[Install]
 WantedBy=multi-user.target
EOF

echo "Creating SD duplicator script..."
cat > ${mntdir}/usr/local/bin/sd-dupe << "EOF"
#!/bin/bash
led 0

src_dev=mmcblk0
dest_dev=mmcblk1
part_num=p5

if [ -e /dev/mmcblk0p5 ]; then
  led 1

  sgdisk -Z -a1 /dev/${dest_dev}
  sgdisk -a1 -n:1:64:8127 -t:1:8301 -c:1:loader1 /dev/${dest_dev}
  sgdisk -a1 -n:2:8128:8191 -t:2:8301 -c:2:env /dev/${dest_dev}
  sgdisk -a1 -n:3:16384:24575 -t:3:8301 -c:3:loader2 /dev/${dest_dev}
  sgdisk -a1 -n:4:24576:32767 -t:4:8301 -c:4:trust /dev/${dest_dev}
  sgdisk -a1 -n:5:32768:- -A:5:set:2 -t:5:8305 -c:5:rootfs /dev/${dest_dev}

  src_block_count=`tune2fs -l /dev/${src_dev}${part_num} | grep "Block count:" | sed 's/.*: *//'`
  src_block_size=`tune2fs -l /dev/${src_dev}${part_num} | grep "Block size:" | sed 's/.*: *//'`
  src_fs_size=$(( src_block_count*src_block_size ))
  src_fs_size_m=$(( src_fs_size / 1024 / 1024 + 1 ))

  dd if=/dev/${src_dev}p1 of=/dev/${dest_dev}p1 conv=sync,noerror status=progress
  dd if=/dev/${src_dev}p2 of=/dev/${dest_dev}p2 conv=sync,noerror status=progress
  dd if=/dev/${src_dev}p3 of=/dev/${dest_dev}p3 conv=sync,noerror status=progress
  dd if=/dev/${src_dev}p4 of=/dev/${dest_dev}p4 conv=sync,noerror status=progress

  echo "Writing ${src_fs_size_m} MB: /dev/${src_dev} -> /dev/${dest_dev}..."
  dd if=/dev/${src_dev}${part_num} of=/dev/${dest_dev}${part_num} bs=1M conv=sync,noerror status=progress

  echo "Expanding /dev/${dest_dev}${part_num} filesystem..."
  e2fsck -fy /dev/${dest_dev}${part_num}
  resize2fs /dev/${dest_dev}${part_num}
  tune2fs -O has_journal /dev/${dest_dev}${part_num}
  e2fsck -fy /dev/${dest_dev}${part_num}
  sync /dev/${dest_dev}

  echo "Cleaning up..."
  mount /dev/${dest_dev}${part_num} /media
  chroot /media /usr/local/bin/install-cleanup

  if [ $? == 0 ]; then
    echo "Successfully copied Rock Pi image!"
    while true; do
      led 1; sleep 0.5
      led 0; sleep 0.5
    done
  else
    echo "Error while copying Rock Pi image"
    while true; do
      led 1; sleep 0.1
      led 0; sleep 0.1
    done
  fi
else
  echo "Expanding /dev/${dest_dev}${part_num} filesystem..."
  e2fsck -fy /dev/${dest_dev}${part_num}
  resize2fs /dev/${dest_dev}${part_num}
  tune2fs -O has_journal /dev/${dest_dev}${part_num}
  e2fsck -fy /dev/${dest_dev}${part_num}
  sync /dev/${dest_dev}

  echo "Cleaning up..."
  /usr/local/bin/install-cleanup
fi
EOF
chmod +x ${mntdir}/usr/local/bin/sd-dupe

echo "Creating SD duplicator service..."
cat > ${mntdir}/etc/systemd/system/sd-dupe.service << EOF
[Unit]
 Description=Duplicate SD card rootfs to eMMC on Rock Pi
 ConditionPathExists=/usr/local/bin/sd-dupe
 After=led.service

[Service]
 Type=simple
 ExecStart=/usr/local/bin/sd-dupe
 TimeoutSec=0
 StandardOutput=tty

[Install]
 WantedBy=multi-user.target
EOF

echo "Creating cleanup script..."
cat > ${mntdir}/usr/local/bin/install-cleanup << "EOF"
#!/bin/bash
echo "Installing cuttlefish-common package..."
echo "nameserver 8.8.8.8" > /etc/resolv.conf
MAC=`ip link | grep eth0 -A1 | grep ether | sed 's/.*\(..:..:..:..:..:..\) .*/\1/'`
sed -i " 1 s/.*/& rockpi-${MAC}/" /etc/hosts
sudo hostnamectl set-hostname "rockpi-${MAC}"

dpkg --add-architecture amd64
until ping -c1 ftp.debian.org; do sleep 1; done
ntpdate time.google.com
while true; do
	apt-get -o Acquire::Check-Valid-Until=false update
	if [ $? != 0 ]; then sleep 1; continue; fi
	apt-get install -y -f libc6:amd64 qemu-user-static
	if [ $? != 0 ]; then sleep 1; continue; fi
	break
done
cd /home/vsoc-01/android-cuttlefish
dpkg-buildpackage -d -uc -us
apt-get install -y -f ../cuttlefish-common_*_arm64.deb
apt-get clean
usermod -aG cvdnetwork vsoc-01
chmod 660 /dev/vhost-vsock
chown root:cvdnetwork /dev/vhost-vsock

rm /etc/machine-id
rm /var/lib/dbus/machine-id
dbus-uuidgen --ensure
systemd-machine-id-setup

systemctl disable sd-dupe
rm /etc/systemd/system/sd-dupe.service
rm /usr/local/bin/sd-dupe
rm /usr/local/bin/install-cleanup
EOF
chmod +x ${mntdir}/usr/local/bin/install-cleanup

chroot ${mntdir} /bin/bash << "EOT"
echo "Enabling services..."
systemctl enable led
systemctl enable sd-dupe

echo "Creating Initial Ramdisk..."
update-initramfs -c -t -k "5.2.0"
mkimage -A arm -O linux -T ramdisk -C none -a 0 -e 0 -n uInitrd -d /boot/initrd.img-5.2.0 /boot/uInitrd-5.2.0
ln -s /boot/uInitrd-5.2.0 /boot/uInitrd
EOT

umount ${mntdir}/sys
umount ${mntdir}/dev
umount ${mntdir}/proc
umount ${mntdir}

if [ ${USE_IMAGE} -eq 0 ]; then
	# 32GB eMMC size
	last_sector=61071326

	device=/dev/${mmc_dev}
	devicep=${device}

	sgdisk -Z -a1 ${device}
	sgdisk -a1 -n:1:64:8127 -t:1:8301 -c:1:loader1 ${device}
	sgdisk -a1 -n:2:8128:8191 -t:2:8301 -c:2:env ${device}
	sgdisk -a1 -n:3:16384:24575 -t:3:8301 -c:3:loader2 ${device}
	sgdisk -a1 -n:4:24576:32767 -t:4:8301 -c:4:trust ${device}
	sgdisk -a1 -n:5:32768:${last_sector} -A:5:set:2 -t:5:8305 -c:5:rootfs ${device}
fi

# Turn on journaling
tune2fs -O ^has_journal ${IMAGE}
e2fsck -fy ${IMAGE} >/dev/null 2>&1

if [ ${USE_IMAGE} -eq 0 ]; then
	dd if=${IMAGE} of=${devicep}5 bs=1M
	resize2fs ${devicep}5 >/dev/null 2>&1
else
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

	# Create final image
	tmpimg=`mktemp`
	truncate -s ${fs_end} ${tmpimg}

	# Create GPT
	sgdisk -Z -a1 ${tmpimg}
	sgdisk -a1 -n:1:64:8127 -t:1:8301 -c:1:loader1 ${tmpimg}
	sgdisk -a1 -n:2:8128:8191 -t:2:8301 -c:2:env ${tmpimg}
	sgdisk -a1 -n:3:16384:24575 -t:3:8301 -c:3:loader2 ${tmpimg}
	sgdisk -a1 -n:4:24576:32767 -t:4:8301 -c:4:trust ${tmpimg}
	sgdisk -a1 -n:5:32768:${end_sector} -A:5:set:2 -t:5:8305 -c:5:rootfs ${tmpimg}

	device=$(losetup -f)
	devicep=${device}p
	losetup ${device} ${tmpimg}
	partx -v --add ${device}

	# copy over data
	dd if=${IMAGE} of=${devicep}5 bs=1M
fi

dd if=${idbloader} of=${devicep}1
dd if=${ANDROID_BUILD_TOP}/external/u-boot/u-boot.itb of=${devicep}3

if [ ${USE_IMAGE} -eq 1 ]; then
	chown $SUDO_USER:`id -ng $SUDO_USER` ${tmpimg}
	mv ${tmpimg} ${IMAGE}
	partx -v --delete ${device}
	losetup -d ${device}
fi
