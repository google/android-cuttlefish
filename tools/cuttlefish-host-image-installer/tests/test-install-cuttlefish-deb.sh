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

echo "t0"
pwd
echo "t1"
ls
echo "t2"
echo "$1"
echo "t3"
find . -name "cuttlefish_packages.7z"
echo "t5"

7z x cuttlefish_packages.7z

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install ./*.deb

sudo usermod -aG cvdnetwork `whoami`

echo "Done"
