#!/bin/sh

apt-get update

# Install necessary packages
apt-get install -y debconf-utils
apt-get install -y ca-certificates
apt-get install -y wget
apt-get install -y git
apt-get install -y python3
apt-get install -y p7zip-full unzip
apt-get install -y iptables ebtables

# Install amd firmware
apt-get install -y firmware-amd-graphics
sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT=\"\(.*\)\"/GRUB_CMDLINE_LINUX_DEFAULT=\"\1 amdgpu.runpm=0 amdgpu.dc=0\"/' /etc/default/grub
dpkg-reconfigure -fnoninteractive grub-efi-arm64

# Adjust user groups
adduser vsoc-01 kvm
adduser vsoc-01 render
adduser vsoc-01 video

# Install Linaro GLT. GIG. repo
wget -qO- https://artifacts.codelinaro.org/artifactory/linaro-372-googlelt-gigabyte-ampere-cuttlefish-installer/gigabyte-ampere-cuttlefish-installer/latest/debian/linaro-glt-gig-archive-bookworm.asc | tee /etc/apt/trusted.gpg.d/linaro-glt-gig-archive-bookworm.asc

echo "deb https://artifacts.codelinaro.org/linaro-372-googlelt-gigabyte-ampere-cuttlefish-installer/gigabyte-ampere-cuttlefish-installer/latest/debian bookworm main" | tee /etc/apt/sources.list.d/linaro-glt-gig-archive-bookworm.list

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 \
    update

# Install kernel
apt-get install -y '^linux-image-6.1.*aosp14-linaro.*' '^linux-headers-6.1.*aosp14-linaro.*'

# Install android cuttlefish packages
apt-get install -y '^cuttlefish-.*'
adduser vsoc-01 cvdnetwork

# Install metapackage
apt-get install -y metapackage-linaro-gigamp

# Extra tools
cd /root
git clone https://github.com/matthuisman/gdrivedl.git
cd -

# Use iptables-legacy
update-alternatives --set iptables /usr/sbin/iptables-legacy

# Install network manager
apt-get install -y network-manager

# Network-manager workaround
rm -f '/etc/NetworkManager/system-connections/Wired connection 1'
