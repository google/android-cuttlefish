#!/bin/bash

set -x
set -o errexit

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

# Now gather all of the *.deb files to copy them into the image
debs=(*.deb)

tmp_debs=()
for i in "${debs[@]}"; do
  tmp_debs+=(/tmp/"$(basename "$i")")
done

# Now install the packages on the disk
sudo mkdir /mnt/image
sudo mount /dev/sdb1 /mnt/image
cp "${debs[@]}" /mnt/image/tmp
sudo mount -t sysfs none /mnt/image/sys
sudo mount -t proc none /mnt/image/proc
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


# Install GPU driver dependencies
sudo chroot /mnt/image /usr/bin/apt install -y gcc
sudo chroot /mnt/image /usr/bin/apt install -y linux-source
sudo chroot /mnt/image /usr/bin/apt install -y linux-headers-`uname -r`
sudo chroot /mnt/image /usr/bin/apt install -y make

# Download the latest GPU driver installer
gsutil cp \
  $(gsutil ls gs://nvidia-drivers-us-public/GRID/GRID*/*-Linux-x86_64-*.run \
    | sort \
    | tail -n 1) \
  /mnt/image/tmp/nvidia-driver-installer.run

# Make GPU driver installer executable
chmod +x /mnt/image/tmp/nvidia-driver-installer.run

# Install the latest GPU driver with default options and the dispatch libs
sudo chroot /mnt/image /tmp/nvidia-driver-installer.run \
  --silent \
  --install-libglvnd

# Cleanup after install
rm /mnt/image/tmp/nvidia-driver-installer.run

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
sudo chroot /mnt/image /usr/bin/apt install -y libvulkan1

# Clean up the builder's version of resolv.conf
sudo rm /mnt/image/etc/resolv.conf

# Skip unmounting:
#  Sometimes systemd starts, making it hard to unmount
#  In any case we'll unmount cleanly when the instance shuts down

echo IMAGE_WAS_CREATED
