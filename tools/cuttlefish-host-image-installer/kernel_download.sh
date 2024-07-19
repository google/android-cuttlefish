#!/bin/sh

KERNEL_MANIFEST_URL_DEFAULT="https://android.googlesource.com/kernel/manifest/"
KERNEL_MANIFEST_BRANCH_DEFAULT="common-android14-6.1"

if [ x"$KERNEL_MANIFEST_URL" = x ]; then
    KERNEL_MANIFEST_URL="$KERNEL_MANIFEST_URL_DEFAULT"
fi

if [ x"$KERNEL_MANIFEST_BRANCH" = x ]; then
    KERNEL_MANIFEST_BRANCH="$KERNEL_MANIFEST_BRANCH_DEFAULT"
fi

mkdir -p kernel-build-space
mkdir -p kernel-build-space/source/kernel

cd kernel-build-space/source/kernel

repo init -u ${KERNEL_MANIFEST_URL} -b ${KERNEL_MANIFEST_BRANCH}
repo sync
repo info -l

cd -
