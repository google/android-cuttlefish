#!/bin/bash

export NODE_ROOT=/tmp/nodejs
export NODE_DISTRO=linux-x64
export NODE_VERSION=v16.17.0
export NODE_HOME=$NODE_ROOT/node-$NODE_VERSION-$NODE_DISTRO
export NODE_SHA256SUM=f0867d7a17a4d0df7dbb7df9ac3f9126c2b58f75450647146749ef296b31b49b # curl https://nodejs.org/dist/$NODE_VERSION/SHASUMS256.txt -O
export PATH=$NODE_HOME/bin:$PATH