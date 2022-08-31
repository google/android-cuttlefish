#!/bin/bash
. setup-nodejs-env.sh

rm -rf $NODE_ROOT
mkdir -p $NODE_ROOT
pushd $NODE_ROOT >/dev/null
curl https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-$NODE_DISTRO.tar.xz -O
curl https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt -O
if ! echo "$NODE_SHA256SUM node-$NODE_VERSION-$NODE_DISTRO.tar.xz" | sha256sum -c ; then
    echo "** ERROR: KEY MISMATCH **"; popd >/dev/null; exit 1;
fi

tar xvf node-$NODE_VERSION-$NODE_DISTRO.tar.xz >/dev/null
popd >/dev/null