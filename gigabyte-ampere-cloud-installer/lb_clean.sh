#!/bin/sh

rm -rf auto
if [ -d binary ]; then
    sudo rm -rf binary
fi
rm -f live-image-arm64.contents
rm -f binary.modified_timestamps live-image-arm64.files
rm -f live-image-arm64.iso
if [ -d cache ]; then
    sudo rm -rf cache
fi
rm -f live-image-arm64.packages
if [ -d chroot ]; then
    sudo rm -rf chroot
fi
rm -rf local
rm -f chroot.files
rm -f chroot.packages.install
rm -f chroot.packages.live
rm -rf config
rm -rf .build
if [ -e chroot.installed_tmp_pkgs ]; then
    sudo rm -f chroot.installed_tmp_pkgs
fi
rm -f uboot_qemu_disk1.img
rm -f uboot_qemu_flash.img
