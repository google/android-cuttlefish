#!/usr/bin/env bash
REPO_OWNER=syslogic
REPO_NAME=android-cuttlefish

git clone https://github.com/${REPO_OWNER}/${REPO_NAME}.git
cd $REPO_NAME || exit
IF [ $$REPO_OWNER -eq syslogic ] ; THEN  git switch rpm-build ; FI
./tools/buildutils/build_packages.sh
tar zcvf ~/android-cuttlefish-src-rpm.tar.gz ~/${REPO_NAME}/tools/rpmbuild/*/*.rpm
la -la ~
