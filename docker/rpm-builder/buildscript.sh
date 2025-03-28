#!/usr/bin/env bash
REPO_OWNER=syslogic
REPO_NAME=android-cuttlefish

git clone https://github.com/${REPO_OWNER}/${REPO_NAME}.git
cd $REPO_NAME || exit
git switch rpm-build

./tools/buildutils/build_packages.sh

tar zcvf /root/android-cuttlefish-src-rpm.tar.gz /root/${REPO_NAME}/tools/rpmbuild/*/*.rpm
