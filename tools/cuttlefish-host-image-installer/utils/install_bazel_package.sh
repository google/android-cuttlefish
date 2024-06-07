#!/bin/sh

echo "Installing bazel"

sudo apt-get install -y npm

mkdir /tmp/bazelisk-t1
cd /tmp/bazelisk-t1
npm install -g @bazel/bazelisk
