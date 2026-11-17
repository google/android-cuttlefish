#!/usr/bin/env bash

# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

GO_VERSION=1.25.14

ARCH=$(uname -m)
if [ "${ARCH}" = "x86_64" ]; then
  ARCH="amd64"
  SHA256="a21ae5633a269bcd7e90cf767e48225633795e99d831742cbf3397064fee7712"
elif [ "${ARCH}" = "aarch64" ]; then
  ARCH="arm64"
  SHA256="9bf234ea70ffec9347fdf6b22ce4add51717d3386a38a441e8c8743fceb5eaee"
else
  echo "Unsupported architecture: ${ARCH}" >&2
  exit 1
fi

if [ "$(cat /usr/local/go/VERSION 2>/dev/null | head -1)" = "go${GO_VERSION}" ]; then
  echo "Go ${GO_VERSION} already installed in /usr/local/go"
  exit 0
fi

apt install -y ca-certificates wget
tmpdir="$(mktemp -t -d golang_installer_XXXXXX)"
trap "rm -rf $tmpdir" EXIT
pushd "${tmpdir}"
tarball="go${GO_VERSION}.linux-${ARCH}.tar.gz"
wget "https://go.dev/dl/${tarball}"
echo "${SHA256}  ${tarball}" | sha256sum -c -
rm -rf /usr/local/go
tar -C /usr/local -xzf "${tarball}"
popd

# Make the Go toolchain binaries (go, gofmt) available under PATH for
# downstream usage.
ln -sf /usr/local/go/bin/go /usr/local/bin/go
ln -sf /usr/local/go/bin/gofmt /usr/local/bin/gofmt
