#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"

# This test creates 2 disks. Each of them are 5G.

tmpdisk1="uboot_qemu_disk1.img"
tmpdisk2="uboot_qemu_disk2.img"

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 5G "${tmpdisk1}"
fi

if [ ! -e "${tmpdisk2}" ]; then
    truncate -s 5G "${tmpdisk2}"
fi
