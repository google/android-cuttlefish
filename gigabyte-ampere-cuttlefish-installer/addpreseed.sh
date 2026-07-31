#!/bin/sh

BASEDIR=$(dirname $(realpath "$0"))

orig_iso=mini.iso
auto_extract_efi=1
efi_img=efi.img

new_files=iso_unpacked_and_modified
new_iso=preseed-mini.iso

PRESEEDFILE=$(realpath "${BASEDIR}"/preseed/preseed.cfg)
AFTERINSTALLSCRIPT=$(realpath "${BASEDIR}"/preseed/after_install_1.sh)

part_img_ready=1
if test "$auto_extract_efi" = 1; then
  start_block=$(/sbin/fdisk -l "$orig_iso" | fgrep "$orig_iso"2 | \
                awk '{print $2}')
  block_count=$(/sbin/fdisk -l "$orig_iso" | fgrep "$orig_iso"2 | \
                awk '{print $4}')
  if test "$start_block" -gt 0 -a "$block_count" -gt 0 2>/dev/null
  then
    dd if="$orig_iso" bs=512 skip="$start_block" count="$block_count" \
       of="$efi_img"
  else
    echo "Cannot read plausible start block and block count from fdisk" >&2
    part_img_ready=0
  fi
fi

# add preseed
mkdir ${new_files}
bsdtar -C ${new_files} -xf "$orig_iso"
cd ${new_files}
cp -f "${PRESEEDFILE}" preseed.cfg
cp -f "${AFTERINSTALLSCRIPT}" after_install_1.sh
chmod a+rx after_install_1.sh

# add preseed to console based installer
chmod ug+w initrd.gz
gzip -d -f initrd.gz
echo preseed.cfg | cpio -H newc -o -A -F initrd
echo after_install_1.sh | cpio -H newc -o -A -F initrd
gzip -9 initrd
chmod a-w initrd.gz
# add preseed to GTK based installer
chmod ug+w gtk
cd gtk
chmod ug+w initrd.gz
gzip -d -f initrd.gz
cp -f ../preseed.cfg .
cp -f ../after_install_1.sh .
echo preseed.cfg | cpio -H newc -o -A -F initrd
echo after_install_1.sh | cpio -H newc -o -A -F initrd
gzip -9 initrd
chmod a-w initrd.gz
rm -f preseed.cfg after_install_1.sh
cd ..
chmod a-w gtk
# modify Graphical installer to use tty1
chmod ug+w boot
chmod ug+w boot/grub
chmod ug+w boot/grub/grub.cfg
sed -i '0,/menuentry/{s#menuentry#menuentry '\''Ampere Install'\'' {\n    set background_color=black\n    linux    /linux --- quiet console=tty1\n    initrd   /gtk/initrd.gz\n}\nmenuentry#}' boot/grub/grub.cfg
sed -i '0,/insmod gzio/{s#insmod gzio#set timeout=120\n\ninsmod gzio#}' boot/grub/grub.cfg
chmod a-w boot/grub/grub.cfg
chmod a-w boot/grub
chmod a-w boot
cd ..

rm -f "${new_iso}"

# Create the new ISO image if not partition extraction failed
test "$part_img_ready" = 1 && \
xorriso -as mkisofs \
   -r -V 'Debian arm64 n' \
   -o "$new_iso" \
   -J -joliet-long -cache-inodes \
   -e boot/grub/efi.img \
   -no-emul-boot \
   -append_partition 2 0xef "$efi_img" \
   -partition_cyl_align all \
   "$new_files"

# clean
rm -f efi.img
chmod ug+w -R "${new_files}"
rm -rf "${new_files}"
