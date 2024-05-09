#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"
#ionice -c 3 -p "$SELFPID"

tmpdisk1="uboot_qemu_disk1.img"
tmpflash="uboot_qemu_flash.img"
uboot="u-boot.bin"
debiancd=preseed-mini*.iso

debiancd=$(realpath ${debiancd})

# We need to backup the debiancd file because qemu doesn't treat
# it as a read-only CD. So the program might be able to change it.

debiancdtmp=$(mktemp -p $(dirname "${debiancd}"))
cp -f "${debiancd}" "${debiancdtmp}"

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 20G "${tmpdisk1}"
fi

# create Flash image for storing U-boot variables.
if [ ! -e "${tmpflash}" ]; then
    qemu-img create -f raw "${tmpflash}" 64M
fi

COUNTER=0
DISKS=""
for DISK1 in uboot_qemu_disk*.img; do
    DISKS="$DISKS -drive file=${DISK1},format=raw,if=none,aio=threads,id=drive-virtio-disk${COUNTER} -device virtio-blk-pci,drive=drive-virtio-disk${COUNTER},iommu_platform=true,disable-legacy=on"
    COUNTER=$((COUNTER+1))
done

qemu-system-aarch64 -machine virt \
     -cpu cortex-a57 \
     -nographic \
     -netdev user,id=net0,hostfwd=tcp::35555-:5555,hostfwd=tcp::33322-:22 \
     -device virtio-net-pci,mac=50:54:00:00:00:56,netdev=net0,id=net0-dev \
     -object rng-builtin,id=objrng0 \
     -device virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,max-bytes=1024,period=2000 \
     -drive if=virtio,format=raw,file="${debiancdtmp}" \
     ${DISKS} \
     -drive if=pflash,format=raw,index=1,file="${tmpflash}" \
     -object cryptodev-backend-builtin,id=cryptodev0 \
     -device virtio-crypto-pci,id=crypto0,cryptodev=cryptodev0 \
     -device virtio-iommu-pci \
     -device virtio-gpu-pci \
     -m 1G \
     -bios "${uboot}"

rm -f "${debiancdtmp}"
