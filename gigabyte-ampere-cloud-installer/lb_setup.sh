#!/bin/sh

DEBIAN_DISTRIBUTION="$(lsb_release -c -s)"

lb config \
    --architectures arm64 \
    --binary-images iso \
    --bootloaders grub-efi \
    --cache false \
    --chroot-filesystem squashfs \
    --chroot-squashfs-compression-type xz \
    --debian-installer-gui false \
    --distribution ${DEBIAN_DISTRIBUTION} \
    --parent-distribution ${DEBIAN_DISTRIBUTION} \
    --parent-debian-installer-distribution ${DEBIAN_DISTRIBUTION} \
    --gzip-options '--best --rsyncable' \
    --bootstrap-qemu-arch arm64 \
    --bootstrap-qemu-static /usr/bin/qemu-aarch64-static

# Modify boot menu to set a timeout
cp -R /usr/share/live/build/bootloaders/grub-pc config/bootloaders/
sed -i "1i set timeout=30" config/bootloaders/grub-pc/config.cfg

# Install packages that necessary
cp -f mydata/*.list.chroot ./config/package-lists/

# Install some scripts
mkdir -p ./config/includes.chroot_after_packages/usr/local/bin/
cp -f mydata/run_after_autologin.sh ./config/includes.chroot_after_packages/usr/local/bin/

# Include Debian cloud image
mkdir -p ./config/includes.chroot_after_packages/usr/local/share/debian-images
if [ -e debian-13-generic-arm64.raw ]; then
    cp -f debian-13-generic-arm64.raw ./config/includes.chroot_after_packages/usr/local/share/debian-images/
fi

# Change root password for debug purpose
cp -f ./mydata/*.hook.chroot ./config/hooks/live/
