#!/bin/sh

sudo apt-get install -y build-essential debhelper fakeroot
sudo apt-get install -y devscripts ubuntu-dev-tools dh-exec
sudo apt-get install -y flex bison bc
sudo apt-get install -y openssl
sudo apt-get install -y rsync openssh-client
sudo apt-get install -y libssl-dev libelf-dev libpci-dev
sudo apt-get install -y libaudit-dev libbabeltrace-dev libdw-dev
sudo apt-get install -y libnewt-dev libnuma-dev libopencsd-dev
sudo apt-get install -y libtraceevent-dev libunwind-dev
sudo apt-get install -y libudev-dev libwrap0-dev libtracefs-dev
sudo apt-get install -y pahole
sudo apt-get install -y kmod sbsigntool
sudo apt-get install -y cpio xz-utils lz4
sudo apt-get install -y crossbuild-essential-arm64
sudo apt-get install -y zlib1g-dev libcap-dev libzstd-dev
sudo apt-get install -y python3 python3-jinja2 dh-python python3-docutils
sudo apt-get install -y libpython3-dev python3-dev python3-setuptools
sudo apt-get install -y libperl-dev
