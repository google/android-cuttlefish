#!/bin/sh

set -e

UBOOT_GIT_URL_DEFAULT=https://source.denx.de/u-boot/u-boot.git
UBOOT_GIT_BRANCH_DEFAULT=master

if [ x"$UBOOT_GIT_URL" = x ]; then
    UBOOT_GIT_URL="$UBOOT_GIT_URL_DEFAULT"
fi

if [ x"$UBOOT_GIT_BRANCH" = x ]; then
    UBOOT_GIT_BRANCH="$UBOOT_GIT_BRANCH_DEFAULT"
fi

git clone --depth=1 --branch="${UBOOT_GIT_BRANCH}" "${UBOOT_GIT_URL}" u-boot
