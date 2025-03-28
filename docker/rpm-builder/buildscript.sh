#!/usr/bin/env bash

# cd /root || exit
# git clone https://github.com/google/android-cuttlefish.git
git clone https://github.com/syslogic/android-cuttlefish.git
cd android-cuttlefish || exit
git switch rpm-build

./tools/buildutils/build_packages.sh

cd tools/rpmbuild/RPMS || exit
tar zcvf /root/android-cuttlefish-rpm.tar.gz /*

cd ../SRPMS || exit
tar zcvf /root/android-cuttlefish-src-rpm.tar.gz /*
