#!/bin/sh

set +e

SELFPID=$$
renice 10 -p "$SELFPID"
ionice -c 3 -p "$SELFPID"

set -e

if [ x"${CI_PIPELINE_ID}" = x ]; then
    export CI_PIPELINE_ID=1
fi

TDIR=kernel-build-space/buildresult
SDIR=kernel-build-space/source/kernel

rm -rf ${TDIR}

mkdir -p ${TDIR}

# Make a copy of AOSP's kernel. deb-pkg target will make the source tree
# unclean. So we make a copy of it and then build inside the copy.
cp -R ${SDIR}/common ${TDIR}/common
cp -R ${SDIR}/common-modules ${TDIR}/common-modules
cp -R ${SDIR}/.repo ${TDIR}/.repo

# Build the kernel
cd ${TDIR}/common
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
KERVER=$(make kernelversion)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig -j32
for i in arch/arm64/configs/gki_defconfig ../common-modules/virtual-device/virtual_device_core.fragment ../common-modules/virtual-device/linux_distro.fragment; do 
    ./scripts/kconfig/merge_config.sh -O . .config ${i}
done
cat <<EOF > ../extraconfig
# CONFIG_MODULE_SIG_PROTECT is not set
# CONFIG_BTRFS_FS is not set
# CONFIG_LOCALVERSION_AUTO is not set
EOF
./scripts/kconfig/merge_config.sh -O . .config ../extraconfig

ANDROIDVERSION=""
if [ x"${KERNEL_MANIFEST_BRANCH}" != x ]; then
    ANDROIDVERSION=$(echo "${KERNEL_MANIFEST_BRANCH}" | sed 's/.*android\([0-9]*\)-.*/\1/')
fi

make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=".aosp${ANDROIDVERSION}-linaro-gig-1-arm64" KDEB_PKGVERSION="${KERVER}"-"${CI_PIPELINE_ID}" deb-pkg -j32

cd -
