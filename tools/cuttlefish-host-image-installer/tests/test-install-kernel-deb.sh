#!/bin/sh

set -e

apt-get install -y sudo
apt-get install -y debconf-utils
echo "tzdata tzdata/Areas select Etc" | debconf-set-selections -v
echo "tzdata tzdata/Zones/Etc select UTC" | debconf-set-selections -v
DEBIAN_FRONTEND=noninteractive apt-get install -y tzdata
dpkg-reconfigure --frontend noninteractive tzdata
apt-get install -y p7zip-full zstd
apt-get install -y initramfs-tools

echo "t0"
pwd
echo "t1"
ls
echo "t2"
echo "$1"
echo "t3"
find . -name "$1"
echo "t5"


7z x $1
apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install ./*.deb
test -e /boot/vmlinuz-*aosp*-linaro*
test -e /boot/initrd.img-*aosp*-linaro*
grep "CONFIG_FHANDLE=y" /boot/config-*aosp*-linaro*
grep 'CONFIG_MODPROBE_PATH="/sbin/modprobe"' /boot/config-*aosp*-linaro*
echo "Done"
