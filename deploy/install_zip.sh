#!/bin/bash

# Installs an Android zip file to a directory

usage() {
  echo cat build.zip \| $0 "\${dir}"
  echo or
  echo $0 build-zip "\${dir}"
}

case $# in
  1)
    mkdir -p "$1"
    bsdtar -x -C "$1" -f -
    unpack_boot_image.py --boot_img "$1/boot.img" --dest "$1"
    ;;
  2)
    mkdir -p "$2"
    bsdtar -x -C "$2" -f "$1"
    unpack_boot_image.py --boot_img "$2/boot.img" --dest "$2"
    ;;
  *)
    usage
    exit 2
    ;;
esac
