#!/bin/bash

VIRGL_PACKAGE=libvirglrenderer-dev

sudo apt update
sudo apt upgrade -y
sudo apt install -y debhelper ubuntu-dev-tools equivs
dpkg -l "${VIRGL_PACKAGE}" || (
  # Try from the distribution
  sudo apt install -y "${VIRGL_PACKAGE}"
  dpkg -l "${VIRGL_PACKAGE}" || (
    echo That failed. Building from source
    curl -L -O http://http.debian.net/debian/pool/main/v/virglrenderer/virglrenderer_0.6.0-2.dsc
    curl -L -O http://http.debian.net/debian/pool/main/v/virglrenderer/virglrenderer_0.6.0.orig.tar.bz2
    curl -L -O http://http.debian.net/debian/pool/main/v/virglrenderer/virglrenderer_0.6.0-2.debian.tar.xz
    sudo mk-build-deps -i virglrenderer_0.6.0-2.dsc -t "apt-get -y"
    rm -f virglrenderer-build-deps_0.6.0-2_all.deb
    dpkg-source -x virglrenderer_0.6.0-2.dsc
    pushd virglrenderer*0
    dpkg-buildpackage -uc -us
    popd
    sudo apt install -y ./*.deb
  )
)
curl -L -O http://http.debian.net/debian/pool/main/q/qemu/qemu_2.12+dfsg.orig.tar.xz
git clone https://salsa.debian.org/qemu-team/qemu.git
pushd qemu
debian/rules debian/control
chmod +w debian/control
popd
sudo mk-build-deps -i qemu/debian/control  -t "apt-get -y"
rm -f ./qemu-build-deps_2.12+dfsg-3_amd64.deb
pushd qemu
dpkg-buildpackage -uc -us --jobs=auto
popd
