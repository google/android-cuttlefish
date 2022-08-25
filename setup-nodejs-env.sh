#!/bin/bash

# Cuttlefish is only supported on 64-bit host architectures
# curl https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt -O
case "$(uname -m)" in
  x86_64)
    export NODE_DISTRO=linux-x64
    export NODE_SHA256SUM=f0867d7a17a4d0df7dbb7df9ac3f9126c2b58f75450647146749ef296b31b49b
    ;;
  aarch64)
    export NODE_DISTRO=linux-arm64
    export NODE_SHA256SUM=a43100595e7960b9e8364bff5641e0956a9929feee2759e70cbb396a1d827b7c
    ;;
esac

export NODE_ROOT=/tmp/nodejs
export NODE_VERSION=v16.17.0
export NODE_HOME=$NODE_ROOT/node-$NODE_VERSION-$NODE_DISTRO
export PATH=$NODE_HOME/bin:$PATH
