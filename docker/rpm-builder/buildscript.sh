#!/usr/bin/env bash
# The file deployed seems stuck.
REPO_NAME=android-cuttlefish
REPO_OWNER=syslogic

git clone https://github.com/${REPO_OWNER}/${REPO_NAME}.git
cd $REPO_NAME || exit
# if [[ $REPO_OWNER = 'syslogic' ]]; then git switch rpm-build; fi
git switch rpm-build;

./tools/buildutils/build_packages.sh
tar zcvf ~/android-cuttlefish-rpm.tar.gz ~/${REPO_NAME}/tools/rpmbuild/*.rpm
ls -la ~
