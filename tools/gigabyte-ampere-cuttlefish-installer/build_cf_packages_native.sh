#!/bin/sh

git clone https://github.com/google/android-cuttlefish.git
cd android-cuttlefish

for subdir in base frontend; do
    cd ${subdir}
    mk-build-deps --install --remove --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes' debian/control
    dpkg-buildpackage -d -uc -us
    cd -
done
