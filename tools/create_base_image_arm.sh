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

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

source "${ANDROID_BUILD_TOP}/external/shflags/src/shflags"

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

FLAGS_HELP="USAGE: $0 <KERNEL_DIR> [IMAGE] [flags]"

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
OVERWRITE=`[ -e "${IMAGE}" ] && echo "1" || echo "0"`
if [ -z $KERNEL_DIR ]; then
	flags_help
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

if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ]; then
	cd ${ANDROID_BUILD_TOP}/external/arm-trusted-firmware
	CROSS_COMPILE=aarch64-linux-gnu- make PLAT=rk3399 DEBUG=0 ERROR_DEPRECATED=1 bl31
	export BL31="${ANDROID_BUILD_TOP}/external/arm-trusted-firmware/build/rk3399/release/bl31/bl31.elf"
	cd -
fi

cd ${ANDROID_BUILD_TOP}/external/u-boot

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
	echo "Sha=`${script_dir}/gen_sha.sh --kernel ${KERNEL_DIR}`" >> ${tmpfile}
	${ANDROID_HOST_OUT}/bin/mkenvimage -s 32768 -o ${bootenv} - < ${tmpfile}
fi

if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ] || [ ${FLAGS_p3} -eq ${FLAGS_TRUE} ]; then
	make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- rock-pi-4-rk3399_defconfig
	if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ]; then
		make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- -j`nproc`
	fi
	if [ ${FLAGS_p3} -eq ${FLAGS_TRUE} ]; then
		make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- u-boot.itb
	fi
	if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ]; then
		idbloader=`mktemp`
		${ANDROID_HOST_OUT}/bin/mkimage -n rk3399 -T rksd -d tpl/u-boot-tpl.bin ${idbloader}
		cat spl/u-boot-spl.bin >> ${idbloader}
	fi
fi
cd -

if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
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
setenv start_poe 'gpio set 150; gpio clear 146'
run start_poe
setenv bootcmd_dhcp '
mw.b ${scriptaddr} 0 0x8000
mmc dev 0 0
mmc read ${scriptaddr} 0x1fc0 0x40
env import -b ${scriptaddr} 0x8000
mw.b ${scriptaddr} 0 0x8000
if dhcp ${scriptaddr} manifest.txt; then
	setenv OldSha ${Sha}
	setenv Sha
	env import -t ${scriptaddr} 0x8000 ManifestVersion
	echo "Manifest version $ManifestVersion";
	if test "$ManifestVersion" = "1"; then
		run manifest1
	elif test "$ManifestVersion" = "2"; then
		run manifest2
	else
		run manifestX
	fi
fi'
setenv manifestX 'echo "***** ERROR: Unknown manifest version! *****";'
setenv manifest1 '
env import -t ${scriptaddr} 0x8000
if test "$Sha" != "$OldSha"; then
	setenv serverip ${TftpServer}
	setenv loadaddr 0x00200000
	mmc dev 0 0;
	setenv file $TplSplImg; offset=0x40; size=0x1f80; run tftpget1; setenv TplSplImg
	setenv file $UbootItb;  offset=0x4000; size=0x2000; run tftpget1; setenv UbootItb
	setenv file $TrustImg; offset=0x6000; size=0x2000; run tftpget1; setenv TrustImg
	setenv file $RootfsImg; offset=0x8000; size=0; run tftpget1; setenv RootfsImg
	setenv file $UbootEnv; offset=0x1fc0; size=0x40; run tftpget1; setenv UbootEnv
	mw.b ${scriptaddr} 0 0x8000
	env export -b ${scriptaddr} 0x8000
	mmc write ${scriptaddr} 0x1fc0 0x40
else
	echo "Already have ${Sha}. Booting..."
fi'
setenv manifest2 '
env import -t ${scriptaddr} 0x8000
if test "$DFUethaddr" = "$ethaddr" || test "$DFUethaddr" = ""; then
	if test "$Sha" != "$OldSha"; then
		setenv serverip ${TftpServer}
		setenv loadaddr 0x00200000
		mmc dev 0 0;
		setenv file $TplSplImg; offset=0x40; size=0x1f80; run tftpget1; setenv TplSplImg
		setenv file $UbootItb;  offset=0x4000; size=0x2000; run tftpget1; setenv UbootItb
		setenv file $TrustImg; offset=0x6000; size=0x2000; run tftpget1; setenv TrustImg
		setenv file $RootfsImg; offset=0x8000; size=0; run tftpget1; setenv RootfsImg
		setenv file $UbootEnv; offset=0x1fc0; size=0x40; run tftpget1; setenv UbootEnv
		mw.b ${scriptaddr} 0 0x8000
		env export -b ${scriptaddr} 0x8000
		mmc write ${scriptaddr} 0x1fc0 0x40
	else
		echo "Already have ${Sha}. Booting..."
	fi
else
	echo "Update ${Sha} is not for me. Booting..."
fi'
setenv tftpget1 '
if test "$file" != ""; then
	mw.b ${loadaddr} 0 0x400000
	tftp ${file}
	if test $? = 0; then
		setenv isGz 0 && setexpr isGz sub .*\\.gz\$ 1 ${file}
		if test $isGz = 1; then
			if test ${file} = ${UbootEnv}; then
				echo "** gzipped env unsupported **"
			else
				setexpr boffset ${offset} * 0x200
				gzwrite mmc 0 ${loadaddr} 0x${filesize} 100000 ${boffset} && echo Updated: ${file}
			fi
		elif test ${file} = ${UbootEnv}; then
			env import -b ${loadaddr} && echo Updated: ${file}
		else
			if test $size = 0; then
				setexpr x $filesize - 1
				setexpr x $x / 0x1000
				setexpr x $x + 1
				setexpr x $x * 0x1000
				setexpr x $x / 0x200
				size=0x${x}
			fi
			mmc write ${loadaddr} ${offset} ${size} && echo Updated: ${file}
		fi
	fi
	if test $? != 0; then
		echo ** UPDATE FAILED: ${file} **
	fi
fi'
if mmc dev 1 0; then; else
	run bootcmd_dhcp;
fi
load mmc ${devnum}:${distro_bootpart} 0x02080000 /boot/Image
load mmc ${devnum}:${distro_bootpart} 0x04000000 /boot/uInitrd
load mmc ${devnum}:${distro_bootpart} 0x01f00000 /boot/dtb/rockchip/rk3399-rock-pi-4.dtb
setenv finduuid "part uuid mmc ${devnum}:${distro_bootpart} uuid"
run finduuid
setenv bootargs "earlycon=uart8250,mmio32,0xff1a0000 console=ttyS2,1500000n8 loglevel=7 root=PARTUUID=${uuid} rootwait rootfstype=ext4 sdhci.debug_quirks=0x20000000 of_devlink=0"
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

	echo "Creating PoE script..."
	cat > ${mntdir}/usr/local/bin/poe << "EOF"
#!/bin/bash

if [ "$1" == "--start" ]; then
	echo 146 > /sys/class/gpio/export
	echo out > /sys/class/gpio/gpio146/direction
	echo 0 > /sys/class/gpio/gpio146/value
	echo 150 > /sys/class/gpio/export
	echo out > /sys/class/gpio/gpio150/direction
	echo 1 > /sys/class/gpio/gpio150/value
	exit 0
fi

if [ "$1" == "--stop" ]; then
	echo 0 > /sys/class/gpio/gpio146/value
	echo 146 > /sys/class/gpio/unexport
	echo 0 > /sys/class/gpio/gpio150/value
	echo 150 > /sys/class/gpio/unexport
	exit 0
fi

if [ ! -e /sys/class/gpio/gpio146/value ] || [ ! -e /sys/class/gpio/gpio150/value ]; then
	echo "error: PoE service not initialized"
	exit 1
fi

if [ "$1" == "0" ] || [ "$1" == "off" ] || [ "$1" == "OFF" ]; then
	echo 0 > /sys/class/gpio/gpio150/value
	exit 0
fi

if [ "$1" == "1" ] || [ "$1" == "on" ] || [ "$1" == "ON" ]; then
	echo 1 > /sys/class/gpio/gpio150/value
	exit 0
fi

echo "usage: poe <0|1>"
exit 1
EOF
	chown root:root ${mntdir}/usr/local/bin/poe
	chmod 755 ${mntdir}/usr/local/bin/poe

	echo "Creating PoE service..."
	cat > ${mntdir}/etc/systemd/system/poe.service << EOF
[Unit]
 Description=PoE service
 ConditionPathExists=/usr/local/bin/poe

[Service]
 Type=oneshot
 ExecStart=/usr/local/bin/poe --start
 ExecStop=/usr/local/bin/poe --stop
 RemainAfterExit=true
 StandardOutput=journal

[Install]
 WantedBy=multi-user.target
EOF

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

if [ -e /dev/mmcblk0p5 ] && [ -e /dev/mmcblk1p5 ]; then
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

	umount ${mntdir}/sys
	umount ${mntdir}/dev
	umount ${mntdir}/proc

	chroot ${mntdir} /bin/bash << "EOT"
echo "Installing cuttlefish-common package..."
dpkg --add-architecture amd64
apt-get update
apt-get install -y -f libc6:amd64 qemu-user-static
cd /home/vsoc-01/android-cuttlefish
dpkg-buildpackage -d -uc -us
apt-get install -y -f ../cuttlefish-common_*_arm64.deb
apt-get clean

usermod -aG cvdnetwork vsoc-01
chmod 660 /dev/vhost-vsock
chown root:cvdnetwork /dev/vhost-vsock
rm -rf /home/vsoc-01/*
EOT

	echo "Creating cleanup script..."
	cat > ${mntdir}/usr/local/bin/install-cleanup << "EOF"
#!/bin/bash
echo "nameserver 8.8.8.8" > /etc/resolv.conf
MAC=`ip link | grep eth0 -A1 | grep ether | sed 's/.*\(..:..:..:..:..:..\) .*/\1/' | tr -d :`
sed -i " 1 s/.*/& rockpi-${MAC}/" /etc/hosts
sudo hostnamectl set-hostname "rockpi-${MAC}"

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
systemctl enable poe
systemctl enable led
systemctl enable sd-dupe

echo "Creating Initial Ramdisk..."
update-initramfs -c -t -k "5.2.0"
mkimage -A arm -O linux -T ramdisk -C none -a 0 -e 0 -n uInitrd -d /boot/initrd.img-5.2.0 /boot/uInitrd-5.2.0
ln -s /boot/uInitrd-5.2.0 /boot/uInitrd
EOT

	umount ${mntdir}

	# Turn on journaling
	tune2fs -O ^has_journal ${IMAGE}
	e2fsck -fy ${IMAGE} >/dev/null 2>&1
fi

if [ ${USE_IMAGE} -eq 0 ]; then
	# 32GB eMMC size
	end_sector=61071326
	device=/dev/${mmc_dev}
	devicep=${device}

	sgdisk -Z -a1 ${device}
	sgdisk -a1 -n:1:64:8127 -t:1:8301 -c:1:loader1 ${device}
	sgdisk -a1 -n:2:8128:8191 -t:2:8301 -c:2:env ${device}
	sgdisk -a1 -n:3:16384:24575 -t:3:8301 -c:3:loader2 ${device}
	sgdisk -a1 -n:4:24576:32767 -t:4:8301 -c:4:trust ${device}
	sgdisk -a1 -n:5:32768:${end_sector} -A:5:set:2 -t:5:8305 -c:5:rootfs ${device}
	if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
		dd if=${IMAGE} of=${devicep}5 bs=1M
		resize2fs ${devicep}5 >/dev/null 2>&1
	fi
else
	device=$(losetup -f)
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
	sgdisk -Z -a1 ${tmpimg}
	sgdisk -a1 -n:1:64:8127 -t:1:8301 -c:1:loader1 ${tmpimg}
	sgdisk -a1 -n:2:8128:8191 -t:2:8301 -c:2:env ${tmpimg}
	sgdisk -a1 -n:3:16384:24575 -t:3:8301 -c:3:loader2 ${tmpimg}
	sgdisk -a1 -n:4:24576:32767 -t:4:8301 -c:4:trust ${tmpimg}
	sgdisk -a1 -n:5:32768:${end_sector} -A:5:set:2 -t:5:8305 -c:5:rootfs ${tmpimg}

	losetup ${device} ${tmpimg}
	partx -v --add ${device}

	if [ ${FLAGS_p5} -eq ${FLAGS_TRUE} ]; then
		dd if=${IMAGE} of=${devicep}5 bs=1M
	fi
fi
if [ ${FLAGS_p1} -eq ${FLAGS_TRUE} ]; then
	dd if=${idbloader} of=${devicep}1
fi
if [ ${FLAGS_p2} -eq ${FLAGS_TRUE} ]; then
	dd if=${bootenv} of=${devicep}2
fi
if [ ${FLAGS_p3} -eq ${FLAGS_TRUE} ]; then
	dd if=${ANDROID_BUILD_TOP}/external/u-boot/u-boot.itb of=${devicep}3
fi
if [ ${USE_IMAGE} -eq 1 ]; then
	chown $SUDO_USER:`id -ng $SUDO_USER` ${tmpimg}
	if [ $OVERWRITE -eq 0 ]; then
		mv ${tmpimg} ${IMAGE}
	fi
	partx -v --delete ${device}
	losetup -d ${device}
fi
