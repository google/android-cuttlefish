#!/usr/bin/env bash

# Cuttlefish is only supported on 64-bit host architectures
# curl https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt -O
case "$(uname -m)" in
  x86_64)
    export NODE_DISTRO=linux-x64
    export NODE_SHA256SUM=69b09dba5c8dcb05c4e4273a4340db1005abeafe3927efda2bc5b249e80437ec
    ;;
  aarch64)
    export NODE_DISTRO=linux-arm64
    export NODE_SHA256SUM=08bfbf538bad0e8cbb0269f0173cca28d705874a67a22f60b57d99dc99e30050
    ;;
esac

export NODE_ROOT=/tmp/nodejs
export NODE_VERSION=v22.14.0
export NODE_HOME=$NODE_ROOT/node-$NODE_VERSION-$NODE_DISTRO
export PATH=$NODE_HOME/bin:$PATH

uninstall_nodejs() {
  rm -rf $NODE_ROOT
}

install_nodejs() {
  uninstall_nodejs
  mkdir -p $NODE_ROOT
  pushd $NODE_ROOT >/dev/null
  curl https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-$NODE_DISTRO.tar.xz -O --retry 3
  curl https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt -O --retry 3
  if ! echo "$NODE_SHA256SUM node-$NODE_VERSION-$NODE_DISTRO.tar.xz" | sha256sum -c ; then
    echo "** ERROR: KEY MISMATCH **"; popd >/dev/null; exit 1;
  fi

  tar xvf node-$NODE_VERSION-$NODE_DISTRO.tar.xz >/dev/null
  popd >/dev/null
}
