#!/bin/bash

set -x
set -o errexit
shopt -s extglob

sudo apt-get update

# Stuff we need to get build support

sudo apt install -y debhelper ubuntu-dev-tools equivs "${extra_packages[@]}"

# Install the cuttlefish build deps

for dsc in *.dsc; do
  yes | sudo mk-build-deps -i "${dsc}" -t apt-get
done

# Installing the build dependencies left some .deb files around. Remove them
# to keep them from landing on the image.
yes | rm -f *.deb

for dsc in *.dsc; do
  # Unpack the source and build it

  dpkg-source -x "${dsc}"
  dir="$(basename "${dsc}" .dsc)"
  dir="${dir/_/-}"
  pushd "${dir}/"
  debuild -uc -us
  popd
done

# Now gather all of the relevant .deb files to copy them into the image
debs=(!(cuttlefish-orchestration*).deb)

tmp_debs=()
for i in "${debs[@]}"; do
  tmp_debs+=(/tmp/"$(basename "$i")")
done

# Now install the packages on the disk
sudo mkdir -p /mnt/image
sudo mount /dev/sdb1 /mnt/image
cp "${debs[@]}" /mnt/image/tmp
sudo mount -t sysfs none /mnt/image/sys
sudo mount -t proc none /mnt/image/proc
sudo mount --bind /boot/efi /mnt/image/boot/efi
sudo mount --bind /dev/ /mnt/image/dev
sudo mount --bind /dev/pts /mnt/image/dev/pts
sudo mount --bind /run /mnt/image/run
# resolv.conf is needed on Debian but not Ubuntu
sudo cp /etc/resolv.conf /mnt/image/etc/
sudo chroot /mnt/image /usr/bin/apt update
sudo chroot /mnt/image /usr/bin/apt install -y "${tmp_debs[@]}"
# install tools dependencies
sudo chroot /mnt/image /usr/bin/apt install -y openjdk-11-jre
sudo chroot /mnt/image /usr/bin/apt install -y unzip bzip2 lzop
sudo chroot /mnt/image /usr/bin/apt install -y aapt
sudo chroot /mnt/image /usr/bin/apt install -y screen # needed by tradefed

sudo chroot /mnt/image /usr/bin/find /home -ls
sudo chroot /mnt/image /usr/bin/apt install -t bullseye-backports -y linux-image-cloud-amd64

# update QEMU version to most recent backport
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-x86 -t bullseye-backports
sudo chroot /mnt/image /usr/bin/apt install -y --only-upgrade qemu-system-arm -t bullseye-backports

# Install GPU driver dependencies
sudo chroot /mnt/image /usr/bin/apt install -y gcc
sudo chroot /mnt/image /usr/bin/apt install -y linux-source
sudo chroot /mnt/image /usr/bin/apt install -y linux-headers-`uname -r`
sudo chroot /mnt/image /usr/bin/apt install -y make
sudo chroot /mnt/image /usr/bin/apt install -y software-properties-common
sudo chroot /mnt/image /usr/bin/add-apt-repository non-free
sudo chroot /mnt/image /usr/bin/add-apt-repository contrib
# TODO rammuthiah rootcause why this line is needed
# For reasons unknown the above two lines don't add non-free and
# contrib to the bullseye backports.
sudo chroot /mnt/image /usr/bin/add-apt-repository 'deb http://deb.debian.org/debian bullseye-backports main non-free contrib'
sudo chroot /mnt/image /usr/bin/apt update

sudo chroot /mnt/image /bin/bash -c 'DEBIAN_FRONTEND=noninteractive /usr/bin/apt install -y nvidia-driver -t bullseye-backports'
sudo chroot /mnt/image /usr/bin/apt install -y firmware-misc-nonfree -t bullseye-backports
sudo chroot /mnt/image /usr/bin/apt install -y libglvnd-dev -t bullseye-backports

# Verify
query_nvidia() {
  sudo chroot /mnt/image nvidia-smi --format=csv,noheader --query-gpu="$@"
}

if [[ $(query_nvidia "count") != "1" ]]; then
  echo "Failed to detect GPU."
  exit 1
fi

if [[ $(query_nvidia "driver_version") == "" ]]; then
  echo "Failed to detect GPU driver."
  exit 1
fi

# Vulkan loader
sudo chroot /mnt/image /usr/bin/apt install -y libvulkan1 -t bullseye-backports

# Wayland-server needed to have Nvidia driver fail gracefully when attemping to
# use the EGL API on GCE instances without a GPU.
sudo chroot /mnt/image /usr/bin/apt install -y libwayland-server0 -t bullseye-backports

# Clean up the builder's version of resolv.conf
sudo rm /mnt/image/etc/resolv.conf

# Make sure the image has /var/empty, and allow unprivileged_userns_clone for
# minijail process sandboxing
sudo chroot /mnt/image /usr/bin/mkdir -p /var/empty
sudo tee /mnt/image/etc/sysctl.d/80-nsjail.conf >/dev/null <<EOF
kernel.unprivileged_userns_clone=1
EOF

# Skip unmounting:
#  Sometimes systemd starts, making it hard to unmount
#  In any case we'll unmount cleanly when the instance shuts down

echo IMAGE_WAS_CREATED
