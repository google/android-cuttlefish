#!/bin/sh

SELFPID=$$
renice 5 -p "$SELFPID"
#ionice -c 3 -p "$SELFPID"

tmpdisk1="edk2_qemu_disk1.img"
edk2vars="/usr/share/AAVMF/AAVMF_VARS.fd"
edk2code="/usr/share/AAVMF/AAVMF_CODE.fd"
tmpflash="AAVMF_VARS.fd"
edk2bios="/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
debiancd=preseed-mini*.iso

debiancd=$(realpath ${debiancd})

# We need to backup the debiancd file because qemu doesn't treat
# it as a read-only CD. So the program might be able to change it.

debiancdtmp=$(mktemp -p $(dirname "${debiancd}"))
cp -f "${debiancd}" "${debiancdtmp}"

if [ ! -e "${tmpdisk1}" ]; then
    truncate -s 20G "${tmpdisk1}"
fi

# create Flash image for storing EDK2 variables.
if [ ! -e "${tmpflash}" ]; then
    cp -f "${edk2vars}" "${tmpflash}"
fi

COUNTER=0
DISKS=""
for DISK1 in edk2_qemu_disk*.img; do
    DISKS="$DISKS -drive file=${DISK1},format=raw,if=none,aio=threads,id=drive-virtio-disk${COUNTER} -device virtio-blk-pci,drive=drive-virtio-disk${COUNTER},iommu_platform=true,disable-legacy=on"
    COUNTER=$((COUNTER+1))
done

qemu-system-aarch64 -machine virt \
     -cpu host \
     -enable-kvm \
     -nographic \
     -netdev user,id=net0,hostfwd=tcp::35555-:5555,hostfwd=tcp::33322-:22 \
     -device virtio-net-pci,mac=50:54:00:00:00:56,netdev=net0,id=net0-dev \
     -object rng-builtin,id=objrng0 \
     -device virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,max-bytes=1024,period=2000 \
     -drive if=pflash,format=raw,readonly=on,file="${edk2code}" \
     -drive if=pflash,format=raw,file="${tmpflash}" \
     -drive if=virtio,format=raw,file="${debiancdtmp}" \
     ${DISKS} \
     -object cryptodev-backend-builtin,id=cryptodev0 \
     -device virtio-crypto-pci,id=crypto0,cryptodev=cryptodev0 \
     -device virtio-iommu-pci \
     -device virtio-gpu-pci \
     -m 1G

rm -f "${debiancdtmp}"
