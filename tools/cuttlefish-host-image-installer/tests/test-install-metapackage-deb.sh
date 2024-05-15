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

7z x meta_gigamp_packages.7z

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install ./*.deb

test -e /etc/security/limits.d/95-google-nofile.conf
ulimit -n

grep time1.google.com /etc/ntpsec/ntp.conf

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 purge metapackage-google

test $(grep time1.google.com /etc/ntpsec/ntp.conf | wc -l) -eq 0
