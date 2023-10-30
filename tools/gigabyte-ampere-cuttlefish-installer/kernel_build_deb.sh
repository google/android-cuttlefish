#!/bin/sh

set +e

SELFPID=$$
renice 10 -p "$SELFPID"
ionice -c 3 -p "$SELFPID"

set -e

TDIR=kernel-build-space/buildresult
SDIR=kernel-build-space/source/kernel

rm -rf ${TDIR}

mkdir -p ${TDIR}

# Download AOSP's kernel
cp -R ${SDIR}/common ${TDIR}/common
cp -R ${SDIR}/common-modules ${TDIR}/common-modules


# Build the kernel
cd ${TDIR}/common
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
KERVER=$(make kernelversion)
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
for i in arch/arm64/configs/gki_defconfig ../common-modules/virtual-device/virtual_device_core.fragment ../common-modules/virtual-device/linux_distro.fragment; do 
    ./scripts/kconfig/merge_config.sh -O . .config ${i}
done
cat <<EOF > ../extraconfig
EOF
#./scripts/kconfig/merge_config.sh -O . .config ../extraconfig

make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION=.aosp-linaro-1-arm64 KDEB_PKGVERSION="${KERVER}"-1 deb-pkg

cd -
