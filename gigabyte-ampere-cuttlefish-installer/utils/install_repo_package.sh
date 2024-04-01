#!/bin/sh

# This script is for CodeLinaro CI. To install repo package in Debian
# image.

# To run this script you need to install build-essential, devscripts,
# ubuntu-dev-tools, equivs, and fakeroot packages first.

mkdir -p repo_debian_package_build_space
cd repo_debian_package_build_space
pull-debian-source repo
cd repo-*
mk-build-deps --install --root-cmd sudo --remove --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes' debian/control
debuild --no-sign
cd ..
sudo apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install ./*.deb
cd ..
