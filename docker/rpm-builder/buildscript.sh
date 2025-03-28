#!/usr/bin/env bash

cd root || exit
# git clone git@github.com:syslogic/android-cuttlefish.git
git clone https://github.com/syslogic/android-cuttlefish.git
cd android-cuttlefish || exit
git switch rpm-build

./tools/buildutils/build_packages.sh
