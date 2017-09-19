#!/bin/bash

# Installs an Android zip file to a directory

usage() {
  echo cat build.zip \| $0 "\${dir}"
  echo or
  echo $0 build-zip "\${dir}"
}

case $# in
  1)
    source=-
    destdir="$1"
    ;;
  2)
    source="$1"
    destdir="$2"
    ;;
  *)
    usage
    exit 2
    ;;
esac

mkdir -p "${destdir}"
bsdtar -x -C "${destdir}" -f "${source}"

/usr/lib/cuttlefish-common/bin/unpack_boot_image.py -boot_img "${destdir}/boot.img" -dest "${destdir}"
for i in cache.img cmdline kernel ramdisk.img system.img userdata.img vendor.img; do
  # Use setfacl so that libvirt does not lose access to this file if user
  # does anything to this file at any point.
  [ -f "${destdir}/${i}" ] && sudo setfacl -m g:libvirt-qemu:rw "${destdir}/${i}"
done
