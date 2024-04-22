#!/bin/sh

set -e

apt-get install -y sudo
apt-get install -y debconf-utils
echo "tzdata tzdata/Areas select Etc" | debconf-set-selections -v
echo "tzdata tzdata/Zones/Etc select UTC" | debconf-set-selections -v
DEBIAN_FRONTEND=noninteractive apt-get install -y tzdata
dpkg-reconfigure --frontend noninteractive tzdata

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install git devscripts config-package-dev debhelper-compat golang curl
apt-get install -y p7zip-full
apt-get install -y openssh-server
apt-get install -y --no-install-recommends udev
apt-get install -y ebtables
whoami
usermod -aG kvm,render `whoami`

7z x cuttlefish_packages.7z

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install ./*.deb

sudo usermod -aG cvdnetwork `whoami`

echo "Done"
