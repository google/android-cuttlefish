#!/usr/bin/env bash

# Cuttlefish is only supported on 64-bit host architectures
# curl https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt -O
case "$(uname -m)" in
  x86_64)
    export NODE_DISTRO=linux-x64
    export NODE_SHA256SUM=2e5d13569282d016861fae7c8f935e741693c269101a5bebcf761a5376d1f99f
    ;;
  aarch64)
    export NODE_DISTRO=linux-arm64
    export NODE_SHA256SUM=1c4a9933a5e45bc88f54f70b5f91232c127ec49f1a5989d23fb85824c7adf9b7
    ;;
esac

export NODE_ROOT=/tmp/nodejs
export NODE_VERSION=v22.22.3
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
