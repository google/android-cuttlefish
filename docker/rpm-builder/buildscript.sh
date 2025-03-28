#!/usr/bin/env bash
REPO_NAME=android-cuttlefish
REPO_OWNER=syslogic

git clone https://github.com/${REPO_OWNER}/${REPO_NAME}.git
cd $REPO_NAME || exit
if [[ ${REPO_OWNER} = syslogic ]]
then
  git switch rpm-build
fi

./tools/buildutils/build_packages.sh
tar zcvf ~/android-cuttlefish-src-rpm.tar.gz ~/${REPO_NAME}/tools/rpmbuild/*.rpm
la -la ~
