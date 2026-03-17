#!/bin/sh

extract_image() {
    disk1e="$1"
    part1e="$2"
    out1e="$3"

    partition_start=$(partx -g -o START -s -n "${part1e}" "${disk1e}" | xargs)
    partition_end=$(partx -g -o END -s -n "${part1e}" "${disk1e}" | xargs)
    partition_num_sectors=$((${partition_end} - ${partition_start} + 1))
    partition_offset=$((${partition_start} * 512))
    partition_size=$((${partition_num_sectors} * 512))

    dd if="${disk1e}" of="${out1e}" bs=512 skip="${partition_start}" \
        count="${partition_num_sectors}"
}

replace_image() {
    disk1e="$1"
    part1e="$2"
    out1e="$3"

    partition_start=$(partx -g -o START -s -n "${part1e}" "${disk1e}" | xargs)
    partition_end=$(partx -g -o END -s -n "${part1e}" "${disk1e}" | xargs)
    partition_num_sectors=$((${partition_end} - ${partition_start} + 1))
    partition_offset=$((${partition_start} * 512))
    partition_size=$((${partition_num_sectors} * 512))

    dd if="${out1e}" of="${disk1e}" bs=512 seek="${partition_start}" \
        count="${partition_num_sectors}" conv=fsync,notrunc
}

# Extract rootfs
root_partition="1"
extract_image debian-13-generic-arm64.raw ${root_partition} rootfs.img

# replace initramfs
e2ls -l rootfs.img:/boot
INITRAMFSFILE=$(e2ls "rootfs.img:/boot/initrd.img-*" | sed 's/[[:space:]]*$//')

if [ ! -e "${INITRAMFSFILE}" ]; then
    echo "Did not found ${INITRAMFSFILE}"
    exit 1
fi

e2cp ${INITRAMFSFILE} rootfs.img:/boot

# replace grub.cfg
e2cp rootfs.img:/boot/grub/grub.cfg .
sed -i 's|root=PARTUUID=[a-z0-9-]\+|root=/dev/mapper/vglinarogigamprootfs-lvlinarogigamprootfs|g' grub.cfg
e2cp grub.cfg rootfs.img:/boot/grub

# replace fstab
e2cp rootfs.img:/etc/fstab .
sed -i 's|PARTUUID=[a-z0-9-]\+\([ ]/[ ]\)|/dev/mapper/vglinarogigamprootfs-lvlinarogigamprootfs\1|g' fstab
e2cp fstab rootfs.img:/etc

# fix rootfs
/usr/sbin/e2fsck -p -f rootfs.img

# place rootfs back
replace_image debian-13-generic-arm64.raw ${root_partition} rootfs.img

# clean temporary file
rm -f rootfs.img
rm -f grub.cfg
rm -f fstab
