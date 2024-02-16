#!/bin/bash

# Copyright (C) 2024 The Android Open Source Project
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

# Build the debian packages in this repository. 

set -e

eval $(grep VERSION_CODENAME /etc/os-release)

[[ ${VERSION_CODENAME} == "bullseye" ]] || { echo "Invalid distribution '${VERSION_CODENAME}'. Use Debian 11 (bullseye)." >&2; exit 1; }

sudo apt install -y debconf-utils debhelper ubuntu-dev-tools equivs

dpkg-source -b base
dpkg-source -b frontend

for dsc in *.dsc; do
  yes | sudo mk-build-deps -i "${dsc}" -t apt-get
done

# Cleanup the `*build-deps_*_all.deb` packages created when installing the build
# dependencies.
yes | rm -f *.deb

for dsc in *.dsc; do
  # Unpack the source and build it
  dpkg-source -x "${dsc}"
  dir="$(basename "${dsc}" .dsc)"
  dir="${dir/_/-}"
  pushd "${dir}/"
  debuild -uc -us
  popd
done

ls -la
